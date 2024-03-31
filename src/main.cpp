#include "main.h"
#include "xkb.h"

#include <iostream>
#include <chrono>
#include <thread>

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTimer>

#include <linux/input-event-codes.h>

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
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    keyRelease(keyCode);
}

int handleText(const QStringList& text, KWtype& wtype)
{
    auto ret = 0;
    auto xkb = Xkb::self();

    for (const auto& string : text) {
        QVector<uint> ucs4_string = string.toUcs4();
        for (const auto& ch : ucs4_string) {
            // std::cout << "Character: 0x" << std::format("{:X}", ch) << "\n";
            xkb_keysym_t keysym = xkb_utf32_to_keysym(ch);
            if (keysym == XKB_KEY_NoSymbol) {
                std::cerr << "Failed to convert character '0x" << std::format("{:X}", ch) << "' to keysym\n";
                ret = 2;
                continue;
            }

            auto keycode = xkb->keycodeFromKeysym(keysym);
            if (!keycode) {
                // Type using CTRL+SHIFT+U <UNICODE HEX>
                wtype.keyPress(KEY_LEFTCTRL);
                wtype.keyPress(KEY_LEFTSHIFT);
                wtype.sendKey(KEY_U);
                wtype.keyRelease(KEY_LEFTCTRL);
                wtype.keyRelease(KEY_LEFTSHIFT);
                std::string ch_hex = std::format("{:x}", ch);
                for(char& ch : ch_hex) {
                    xkb_keysym_t keysym = xkb_utf32_to_keysym(ch);
                    auto keycode = xkb->keycodeFromKeysym(keysym);
                    wtype.sendKey(keycode->code);
                }
                wtype.sendKey(KEY_SPACE);
                continue;
            }

            switch (keycode->level) {
            case 0:
                wtype.sendKey(keycode->code);
                break;
            case 1:
                wtype.keyPress(KEY_LEFTSHIFT);
                wtype.sendKey(keycode->code);
                wtype.keyRelease(KEY_LEFTSHIFT);
                break;
            case 2:
                wtype.keyPress(KEY_RIGHTALT);
                wtype.sendKey(keycode->code);
                wtype.keyRelease(KEY_RIGHTALT);
                break;
            case 3:
                wtype.keyPress(KEY_LEFTSHIFT);
                wtype.keyPress(KEY_RIGHTALT);
                wtype.sendKey(keycode->code);
                wtype.keyRelease(KEY_LEFTSHIFT);
                wtype.keyRelease(KEY_RIGHTALT);
                break;
            default:
                std::cerr << "Unsupported key level: " << (keycode->level + 1) << ", key code: " << keycode->code << "\n";
                ret = 2;
                break;
            }
        }
    }

    return ret;
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
    parser.addPositionalArgument("text", QCoreApplication::translate("main", "Text to type"));
    parser.process(app);
    const QStringList text = parser.positionalArguments();

    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, []{
        std::cerr << "Failed to authenticate fake input protoccol within timeout\n";
        QCoreApplication::exit(1);
    });
    timer.start(1000);

    KWtype wtype(&app);
    QObject::connect(&wtype, &KWtype::authenticated, [&text, &wtype, &timer] {
        QObject::connect(&wtype, &KWtype::keymapChanged, [&text, &wtype, &timer] {
            timer.stop();
            auto ret = handleText(text, wtype);
            QCoreApplication::exit(ret);
        });
    });

    return app.exec();
}
