#include "main.h"
#include "xkb.h"

#include <iostream>
#include <chrono>
#include <thread>

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTimer>

#include <linux/input-event-codes.h>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>

KWtype::KWtype(QObject *parent)
    : QObject(parent)
    , m_connectionThread(new QThread(this))
    , m_connectionThreadObject(new ConnectionThread())
{
    init();
}

KWtype::~KWtype()
{
    quit();
}

void KWtype::quit() {
    m_connectionThread->quit();
    m_connectionThread->wait();
    m_connectionThreadObject->deleteLater();
    if (m_fakeInput)
        m_fakeInput->release();
    if (m_seat)
        m_seat->release();
    if (m_registry)
        m_registry->release();
}

void KWtype::init()
{
    connect(
        m_connectionThreadObject,
        &ConnectionThread::connected,
        this,
        [this] {
            m_eventQueue = new EventQueue(this);
            m_eventQueue->setup(m_connectionThreadObject);

            m_registry = new Registry(this);
            setupRegistry();
        },
        Qt::QueuedConnection
    );;

    m_connectionThreadObject->moveToThread(m_connectionThread);
    m_connectionThread->start();
    m_connectionThreadObject->initConnection();
}

void KWtype::setupRegistry()
{
    connect(m_registry, &Registry::fakeInputAnnounced, this, [this](quint32 name, quint32 version) {
        m_fakeInput = m_registry->createFakeInput(name, version, this);
        if (m_fakeInput) {
            m_fakeInput->authenticate(QStringLiteral("KWtype"), QStringLiteral("KDE Virtual Keyboard Input"));
            Q_EMIT authenticated();
        }
    });

    connect(m_registry, &Registry::seatAnnounced, this, [this](quint32 name, quint32 version) {
        m_seat = m_registry->createSeat(name, version, this);
        connect(m_seat, &Seat::hasKeyboardChanged, this, [this](bool has) {
            if (!has || m_keyboard) {
                return;
            }
            m_keyboard = m_seat->createKeyboard(this);
            connect(m_keyboard, &Keyboard::keymapChanged, this, [this](int fd, quint32 size) {
                Xkb::self()->keyboardKeymap(fd, size);
                Q_EMIT keymapChanged();
            });
        });
    });

    m_registry->setEventQueue(m_eventQueue);
    m_registry->create(m_connectionThreadObject);
    m_registry->setup();
}

void KWtype::keyPress(quint32 keyCode)
{
    m_fakeInput->requestKeyboardKeyPress(keyCode);
}

void KWtype::keyRelease(quint32 keyCode)
{
    m_fakeInput->requestKeyboardKeyRelease(keyCode);
}

void KWtype::sendKey(quint32 keyCode)
{
    keyPress(keyCode);
    if (!noFlush) {
        m_connectionThreadObject->flush();
    }
    sleep(keyHold);
    keyRelease(keyCode);
    if (!noFlush) {
        m_connectionThreadObject->flush();
    }
}

static uint32_t kwinGetLayout()
{
    auto msg = QDBusMessage::createMethodCall(
        "org.kde.keyboard", "/Layouts",
        "org.kde.KeyboardLayouts", "getLayout");
    QDBusReply<uint> reply = QDBusConnection::sessionBus().call(msg);
    return reply.isValid() ? reply.value() : 0;
}

static void kwinSetLayout(uint32_t idx)
{
    auto msg = QDBusMessage::createMethodCall(
        "org.kde.keyboard", "/Layouts",
        "org.kde.KeyboardLayouts", "setLayout");
    msg << static_cast<uint>(idx);
    QDBusConnection::sessionBus().call(msg);
}

int KWtype::handleText(const QStringList& text)
{
    auto ret = 0;
    auto xkb = Xkb::self();
    auto stringFinalIdx = text.size() - 1;
    uint32_t originalLayout = kwinGetLayout();
    uint32_t currentLayout = originalLayout;

    for (auto string = text.begin(); string != text.end(); ++string) {
        auto stringIdx = std::distance(text.begin(), string);
        QVector<uint> ucs4String = string->toUcs4();
        auto chFinalIdx = ucs4String.size() - 1;

        for (auto chp = ucs4String.begin(); chp != ucs4String.end(); ++chp) {
            auto chIdx = std::distance(ucs4String.begin(), chp);
            auto ch = *chp;
            xkb_keysym_t keysym = xkb_utf32_to_keysym(ch);
            if (keysym == XKB_KEY_NoSymbol) {
                std::cerr << "Failed to convert character '0x" << std::format("{:X}", ch) << "' to keysym\n";
                ret = 2;
                continue;
            }

            auto keycode = xkb->keycodeFromKeysym(keysym);
            if (!keycode) {
                // Type using CTRL+SHIFT+U <UNICODE HEX>
                keyPress(KEY_LEFTCTRL);
                keyPress(KEY_LEFTSHIFT);
                sendKey(KEY_U);
                keyRelease(KEY_LEFTCTRL);
                keyRelease(KEY_LEFTSHIFT);
                std::string ch_hex = std::format("{:x}", ch);
                for(char& ch : ch_hex) {
                    xkb_keysym_t keysym = xkb_utf32_to_keysym(ch);
                    auto keycode = xkb->keycodeFromKeysym(keysym);
                    sendKey(keycode->code);
                }
                sendKey(KEY_SPACE);
                continue;
            }

            // Switch layout via KWin DBus if needed
            uint32_t targetLayout = keycode->layout;
            if (currentLayout != targetLayout) {
                kwinSetLayout(targetLayout);
                // Wait until KWin confirms the layout change
                for (int i = 0; i < 50; i++) {
                    sleep(5);
                    if (kwinGetLayout() == targetLayout) break;
                }
                sleep(20);
                currentLayout = targetLayout;
            }

            switch (keycode->level) {
            case 0:
                sendKey(keycode->code);
                break;
            case 1:
                keyPress(KEY_LEFTSHIFT);
                sendKey(keycode->code);
                keyRelease(KEY_LEFTSHIFT);
                break;
            case 2:
                keyPress(KEY_RIGHTALT);
                sendKey(keycode->code);
                keyRelease(KEY_RIGHTALT);
                break;
            case 3:
                keyPress(KEY_LEFTSHIFT);
                keyPress(KEY_RIGHTALT);
                sendKey(keycode->code);
                keyRelease(KEY_LEFTSHIFT);
                keyRelease(KEY_RIGHTALT);
                break;
            default:
                std::cerr << "Unsupported key level: " << (keycode->level + 1) << ", key code: " << keycode->code << "\n";
                ret = 2;
                break;
            }
            if (keyDelay != 0 && (stringIdx != stringFinalIdx || chIdx != chFinalIdx)) {
                sleep(keyDelay);
            }
        }
    }

    // Restore original layout
    if (currentLayout != originalLayout) {
        kwinSetLayout(originalLayout);
    }

    return ret;
}

void sleep(qint32 ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("KWtype");
    QCoreApplication::setApplicationVersion("0.1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Virtual keyboard input tool for KDE Wayland");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption noFlushOpt(QStringList() << "no-flush",
            QCoreApplication::translate("main", "Do not flush the wayland connection after each key press/release"));
    parser.addOption(noFlushOpt);
    QCommandLineOption keyDelayOpt(QStringList() << "d" << "key-delay",
            QCoreApplication::translate("main", "Delay N milliseconds between keys (the delay between each key press/release pair)"),
            QCoreApplication::translate("main", "delay"));
    parser.addOption(keyDelayOpt);
    QCommandLineOption keyHoldOpt(QStringList() << "H" << "key-hold",
            QCoreApplication::translate("main", "Hold each key for N milliseconds (the delay between key press and release)"),
            QCoreApplication::translate("main", "hold"));
    parser.addOption(keyHoldOpt);
    parser.addPositionalArgument("text", QCoreApplication::translate("main", "Text to type"));
    parser.process(app);

    KWtype wtype(&app);
    if (parser.isSet(noFlushOpt)) {
        wtype.noFlush = true;
    }
    if (parser.isSet(keyDelayOpt)) {
        QString keyDelayStr = parser.value(keyDelayOpt);
        wtype.keyDelay = keyDelayStr.toUInt();
    }
    if (parser.isSet(keyHoldOpt)) {
        QString keyHoldStr = parser.value(keyHoldOpt);
        wtype.keyHold = keyHoldStr.toUInt();
    }
    const QStringList text = parser.positionalArguments();

    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, []{
        std::cerr << "Failed to authenticate fake input protoccol within timeout\n";
        QCoreApplication::exit(1);
    });
    timer.start(1000);

    QObject::connect(&wtype, &KWtype::authenticated, [&] {
        QObject::connect(&wtype, &KWtype::keymapChanged, [&] {
            timer.stop();
            auto ret = wtype.handleText(text);
            QCoreApplication::exit(ret);
        });
    });

    return app.exec();
}
