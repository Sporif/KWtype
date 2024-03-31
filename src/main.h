#pragma once

#include <QObject>
#include <QThread>

#include <KWayland/Client/connection_thread.h>
#include <KWayland/Client/event_queue.h>
#include <KWayland/Client/registry.h>
#include <KWayland/Client/seat.h>
#include <KWayland/Client/keyboard.h>
#include <KWayland/Client/fakeinput.h>

using namespace KWayland::Client;

class KWtype : public QObject
{
    Q_OBJECT
public:
    explicit KWtype(QObject *parent = nullptr);
    ~KWtype() override;

    void keyPress(quint32 linuxKey);
    void keyRelease(quint32 linuxKey);
    void sendKey(quint32 linuxKey);

Q_SIGNALS:
    void authenticated();
    void keymapChanged();

private:
    void init();
    void setupRegistry();
    void quit();

    QThread *m_connectionThread = nullptr;
    ConnectionThread *m_connectionThreadObject = nullptr;
    EventQueue *m_eventQueue = nullptr;
    Registry *m_registry = nullptr;
    Seat *m_seat = nullptr;
    Keyboard *m_keyboard = nullptr;
    FakeInput *m_fakeInput = nullptr;
};
