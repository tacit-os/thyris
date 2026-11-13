#include "input.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QStyleHints>
#include <QWheelEvent>

uint32_t Input::modifiers(Qt::KeyboardModifiers keys, Qt::MouseButtons buttons) {
    uint32_t m = EVENTFLAG_NONE;
    if (keys & Qt::ShiftModifier)
        m |= EVENTFLAG_SHIFT_DOWN;
    if (keys & Qt::ControlModifier)
        m |= EVENTFLAG_CONTROL_DOWN;
    if (keys & Qt::AltModifier)
        m |= EVENTFLAG_ALT_DOWN;
    if (keys & Qt::MetaModifier)
        m |= EVENTFLAG_COMMAND_DOWN;
    if (keys & Qt::KeypadModifier)
        m |= EVENTFLAG_IS_KEY_PAD;
    if (buttons & Qt::LeftButton)
        m |= EVENTFLAG_LEFT_MOUSE_BUTTON;
    if (buttons & Qt::MiddleButton)
        m |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;
    if (buttons & Qt::RightButton)
        m |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
    return m;
}

CefMouseEvent Input::mouse(QPointF pos, Qt::KeyboardModifiers keys, Qt::MouseButtons buttons) {
    CefMouseEvent e;
    e.x = qRound(pos.x());
    e.y = qRound(pos.y());
    e.modifiers = modifiers(keys, buttons);
    return e;
}

// A US layout's virtual key at a place on the keyboard, by xkb's key code (the evdev code + 8, Qt's nativeScanCode), as
// Chromium falls back to for a key whose character names none: Russian л is K, AZERTY's unshifted & is 1, Shift+1's ! is 1.
static int usLayoutKey(quint32 code) {
    if (code >= 10 && code <= 19)
        return "1234567890"[code - 10];
    if (code >= 24 && code <= 33)
        return "QWERTYUIOP"[code - 24];
    if (code >= 38 && code <= 46)
        return "ASDFGHJKL"[code - 38];
    if (code >= 52 && code <= 58)
        return "ZXCVBNM"[code - 52];
    switch (code) {
    case 20: return 0xBD;   // - _
    case 21: return 0xBB;   // = +
    case 34: return 0xDB;   // [ {
    case 35: return 0xDD;   // ] }
    case 47: return 0xBA;   // ; :
    case 48: return 0xDE;   // ' "
    case 49: return 0xC0;   // ` ~
    case 51: return 0xDC;   // backslash |
    case 59: return 0xBC;   // , <
    case 60: return 0xBE;   // . >
    case 61: return 0xBF;   // / ?
    case 94: return 0xE2;   // the key between the left Shift and Z
    default: return 0;
    }
}

// Windows' virtual key codes, which Chromium uses for keyCode on every platform
static int windowsKey(const QKeyEvent *event) {
    const int k = event->key();
    // the keypad's digits and signs have codes of their own (with Num Lock off its keys are Home, End and the arrows)
    if (event->modifiers() & Qt::KeypadModifier) {
        if (k >= Qt::Key_0 && k <= Qt::Key_9)
            return 0x60 + (k - Qt::Key_0);
        switch (k) {
        case Qt::Key_Asterisk: return 0x6A;
        case Qt::Key_Plus: return 0x6B;
        case Qt::Key_Minus: return 0x6D;
        case Qt::Key_Period: case Qt::Key_Comma: return 0x6E;
        case Qt::Key_Slash: return 0x6F;
        default: break;
        }
    }
    if (k >= Qt::Key_A && k <= Qt::Key_Z)
        return 'A' + (k - Qt::Key_A);
    if (k >= Qt::Key_0 && k <= Qt::Key_9)
        return '0' + (k - Qt::Key_0);
    if (k >= Qt::Key_F1 && k <= Qt::Key_F24)
        return 0x70 + (k - Qt::Key_F1);
    switch (k) {
    case Qt::Key_Backspace: return 0x08;
    case Qt::Key_Tab: case Qt::Key_Backtab: return 0x09;
    case Qt::Key_Return: case Qt::Key_Enter: return 0x0D;
    case Qt::Key_Shift: return 0x10;
    case Qt::Key_Control: return 0x11;
    case Qt::Key_Alt: case Qt::Key_AltGr: return 0x12;
    case Qt::Key_Pause: return 0x13;
    case Qt::Key_CapsLock: return 0x14;
    case Qt::Key_Escape: return 0x1B;
    case Qt::Key_Space: return 0x20;
    case Qt::Key_PageUp: return 0x21;
    case Qt::Key_PageDown: return 0x22;
    case Qt::Key_End: return 0x23;
    case Qt::Key_Home: return 0x24;
    case Qt::Key_Left: return 0x25;
    case Qt::Key_Up: return 0x26;
    case Qt::Key_Right: return 0x27;
    case Qt::Key_Down: return 0x28;
    case Qt::Key_Print: return 0x2C;
    case Qt::Key_Insert: return 0x2D;
    case Qt::Key_Delete: return 0x2E;
    case Qt::Key_Help: return 0x2F;
    case Qt::Key_Meta: case Qt::Key_Super_L: return 0x5B;
    case Qt::Key_Super_R: return 0x5C;
    case Qt::Key_Menu: return 0x5D;
    case Qt::Key_NumLock: return 0x90;
    case Qt::Key_ScrollLock: return 0x91;
    case Qt::Key_VolumeMute: return 0xAD;
    case Qt::Key_VolumeDown: return 0xAE;
    case Qt::Key_VolumeUp: return 0xAF;
    case Qt::Key_MediaNext: return 0xB0;
    case Qt::Key_MediaPrevious: return 0xB1;
    case Qt::Key_MediaStop: return 0xB2;
    case Qt::Key_MediaTogglePlayPause: case Qt::Key_MediaPlay: return 0xB3;
    default: break;
    }
    // punctuation: Windows' OEM codes, by the character the key gives on the person's layout (a US layout's names)
    const QString text = event->text();
    const QChar c = text.size() == 1 ? text.at(0) : (k > 0 && k < 0x10000 ? QChar(k) : QChar());
    switch (c.toLatin1()) {
    case ';': case ':': return 0xBA;
    case '=': case '+': return 0xBB;
    case ',': case '<': return 0xBC;
    case '-': case '_': return 0xBD;
    case '.': case '>': return 0xBE;
    case '/': case '?': return 0xBF;
    case '`': case '~': return 0xC0;
    case '[': case '{': return 0xDB;
    case '\\': case '|': return 0xDC;
    case ']': case '}': return 0xDD;
    case '\'': case '"': return 0xDE;
    default: break;
    }
    if (c.isLetterOrNumber() && c.unicode() < 0x80)
        return c.toUpper().unicode();
    // a letter of another script (Cyrillic, Greek, Arabic, Hebrew, ü), a digit-row key typing a sign (AZERTY's & é ",
    // Shift+1's !), a dead key: the key at that place on a US layout, as Chromium gives, where it was 0
    return usLayoutKey(event->nativeScanCode());
}

CefKeyEvent Input::key(const QKeyEvent *event, cef_key_event_type_t type) {
    CefKeyEvent e;
    e.type = type;
    e.modifiers = modifiers(event->modifiers(), {});
    if (event->isAutoRepeat())
        e.modifiers |= EVENTFLAG_IS_REPEAT;
    e.windows_key_code = windowsKey(event);
    // Wayland's and X11's keycode for the key itself, which Chromium turns into the DOM `code`
    e.native_key_code = int(event->nativeScanCode());
    e.is_system_key = (event->modifiers() & Qt::AltModifier) != 0;
    const QString text = keyText(event);
    e.character = text.isEmpty() ? 0 : text.at(0).unicode();
    e.unmodified_character = e.character;
    return e;
}

// The text a key types, as Chromium takes it for the character event: Enter's carriage return even when Qt gives no text
// with it (a key made by an input method, an on-screen keyboard or the checks), since Blink submits a form on that
// character, not on the key down; Qt WebEngine does the same.
QString Input::keyText(const QKeyEvent *event) {
    if (event->text().isEmpty() && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter))
        return QStringLiteral("\r");
    return event->text();
}

QPointF Input::wheelPixels(const QWheelEvent *event) {
    static const int lines = QGuiApplication::styleHints()->wheelScrollLines();
    return QPointF(event->angleDelta()) * (lines * 20.0 / QWheelEvent::DefaultDeltasPerStep);
}
