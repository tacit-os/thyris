import QtQuick
import QtTest
import "../qml/Asks.js" as Asks
import "../qml/Logins.js" as Logins
import "../qml/Settings.js" as Settings

// Passwords (PLAN.md 7, 3.15): what the browser's script world is given to run, how a message from it is read, the
// card's "Save password for <host>?" in the stacked-state order, the Fill rows, and Settings > Passwords' rows.
TestCase {
    name: "Passwords"

    readonly property var entries: [
        { id: "/org/freedesktop/secrets/collection/login/1", origin: "https://github.com", user: "abdul" },
        { id: "/org/freedesktop/secrets/collection/login/2", origin: "https://github.com", user: "work" },
        { id: "/org/freedesktop/secrets/collection/login/3", origin: "http://router.lan", user: "" },
    ]

    function test_theScriptsParseAndTheWatchSendsNoFormInAPrivateWindow() {
        for (const source of [Logins.watcher(true), Logins.watcher(false), Logins.fill("https://github.com", "a'b\"c", "p\\w</script>\n")])
            verify(new Function("return " + source) !== null, "each is one expression that runs");
        verify(Logins.watcher(true).indexOf("'submit'") >= 0, "the main profile's watch reports a sent login form");
        verify(Logins.watcher(false).indexOf("submit") < 0, "a private window's never does: nothing can offer to save there");
        verify(Logins.watcher(true).indexOf("e.isTrusted") >= 0, "only a form you sent: a page cannot make one up");
        verify(Logins.watcher(true).indexOf("armed.form === f") >= 0,
               "and only after your Enter or click on it: a script's requestSubmit() is trusted too (engine check: passwords)");
        verify(Logins.watcher(false).indexOf("'focusin', e => { if (!e.isTrusted) return;") >= 0, "a focus event the page makes up says nothing");
        verify(Logins.watcher(true).startsWith("(() => { if (window !== window.top) return;"), "top documents only");
        const fill = Logins.fill("https://github.com", "abdul", "hunter2");
        verify(fill.indexOf("location.origin !== s.origin") >= 0, "it fills only while the page is at the login's origin");
        verify(fill.indexOf(JSON.stringify({ origin: "https://github.com", user: "abdul", password: "hunter2" })) >= 0,
               "what it fills goes in as JSON, never as script");
    }

    // the role the watch sends for a text field taking the focus, the script run against a stand-in page that has only it
    function roleOf(field) {
        const sent = [], on = {};
        class Input {
            constructor(a) { Object.assign(this, { type: "text", name: "", id: "", placeholder: "", autocomplete: "", form: null }, a) }
            getAttribute(n) { return n === "aria-label" ? this.label ?? null : null }
            getRootNode() { return { querySelectorAll: () => [this] } }
        }
        const e = new Input(field), win = {};
        win.top = win;
        new Function("window", "addEventListener", "HTMLInputElement", "qt", "location", "document", "return " + Logins.watcher(false))(
            win, (type, f) => on[type] = f, Input, { webChannelTransport: { send: m => sent.push(JSON.parse(JSON.parse(m).args[0])) } },
            { origin: "https://pay.test" }, { hasFocus: () => true });
        on.focusin({ isTrusted: true, composedPath: () => [e], target: e });
        return sent.length > 0 ? sent[0].role : "";
    }

    function test_theNotchOffersAPhoneCodeForAOneTimeCodesFieldOnly() {
        compare(roleOf({ autocomplete: "one-time-code" }), "code");
        compare(roleOf({ name: "otp" }), "code");
        compare(roleOf({ id: "verification-code", type: "tel" }), "code");
        compare(roleOf({ name: "otc", placeholder: "Enter code" }), "code", "Microsoft's");
        compare(roleOf({ name: "otc", label: "Security code" }), "code", "a bank's or Microsoft's code from the phone, called a security code");
        compare(roleOf({ name: "pin", type: "number" }), "code");
        compare(roleOf({ name: "postal_code", placeholder: "ZIP code" }), "", "a ZIP or postal code is not one");
        compare(roleOf({ name: "billing-postal-code" }), "");
        compare(roleOf({ name: "code", autocomplete: "postal-code" }), "", "nor what autocomplete says is a postal code");
        compare(roleOf({ name: "promo_code", placeholder: "Promo code" }), "", "nor a promo, coupon or gift card's code");
        compare(roleOf({ placeholder: "Coupon code" }), "");
        compare(roleOf({ name: "cvv", placeholder: "Security code" }), "", "nor a card's security code");
        compare(roleOf({ name: "security_code", autocomplete: "cc-csc" }), "");
        compare(roleOf({ name: "search" }), "", "a plain text field with no password is nothing");
    }

    function test_aMessageIsReadOnlyForThePagesOwnOrigin() {
        const m = Logins.parse(JSON.stringify({ origin: "https://github.com", user: "abdul", password: "x" }), "https://github.com");
        compare(m.user, "abdul");
        compare(Logins.parse(JSON.stringify({ origin: "https://github.com", role: "user" }), "https://gist.github.com"), null,
                "the page went on to another origin since");
        compare(Logins.parse("not json", "https://github.com"), null);
        compare(Logins.parse(JSON.stringify({ origin: "null", role: "user" }), "null"), null, "a data: or file: page saves nothing");
        compare(Logins.parse(JSON.stringify({ origin: "file://", role: "user" }), "file://"), null);
        // the page's own origin is punycode; the view's address, read as BrowserView reads it, has the name decoded
        const idn = Logins.parse(JSON.stringify({ origin: "https://xn--bcher-kva.de", role: "user" }), Asks.originOf(String(Qt.resolvedUrl("https://xn--bcher-kva.de/login"))));
        compare(idn ? idn.origin : null, "https://xn--bcher-kva.de", "a site whose name is not ASCII, its origin kept as the page sent it");
        compare(Logins.parse(JSON.stringify({ origin: "https://xn--bcher-kva.de", role: "user" }), "https://xn--bcher-kva.com"), null);
        compare(Logins.parse(JSON.stringify({ origin: "http://[fe80::1]:8080", role: "user" }), Asks.originOf("http://[fe80::1]:8080/admin")).role, "user");
    }

    function test_theLoginsOfAnOriginAndHowTheyRead() {
        compare(Logins.forOrigin(entries, "https://github.com").map(e => e.user), ["abdul", "work"]);
        compare(Logins.forOrigin(entries, "http://github.com"), [], "http is another origin than https");
        compare(Logins.forOrigin(entries, ""), []);
        compare(Logins.saved(entries, "https://github.com", "work").id, entries[1].id);
        compare(Logins.saved(entries, "https://github.com", "nobody"), null);
        compare(Logins.host("https://www.github.com"), "github.com", "as the address card reads a host");
        compare(Logins.host("http://router.lan"), "http://router.lan", "plain http says so");
        compare(Logins.host("https://127.0.0.1:8443"), "127.0.0.1:8443");
        const rows = Logins.fillRows(Logins.forOrigin(entries, "http://router.lan").concat(entries.slice(0, 1)));
        compare(rows.map(r => [r.kind, r.glyph, r.title, r.detail]), [["fill", "key", "http://router.lan", "Fill"], ["fill", "key", "abdul", "Fill"]],
                "a suggestions' row each: the user name, or the host for a login saved without one");
        compare(rows[1].entry, entries[0]);
    }

    function test_whyASaveOrAFillDidNotHappen() {
        compare(Logins.failed("save", "none"), "Not saved: no keyring answers");
        compare(Logins.failed("save", "locked"), "Not saved: the keyring stayed locked");
        compare(Logins.failed("fill", "form"), "Not filled: no login form here");
        compare(Logins.failed("fill", "failed"), "Not filled: the keyring refused it");
        compare(Logins.failed("show", "locked"), "Not shown: the keyring stayed locked");
    }

    function test_theSaveLineInTheCardsOrder() {
        const pw = { id: 1, kind: "password", origin: "https://github.com", host: "github.com", user: "abdul", password: "x" };
        const external = { id: 2, kind: "external", host: "github.com", scheme: "zoommtg", app: "Zoom", url: "zoommtg://x" };
        const leave = { id: 3, kind: "leave", host: "github.com" };
        compare(Asks.order([pw, external, leave]).map(a => a.kind), ["leave", "external", "password"],
                "PLAN.md 11: page-blocking asks, then external app, then save password");
        const save = { id: 4, kind: "save", url: "https://github.com", title: "GitHub" };
        compare(Asks.order([pw, save]).map(a => a.id), [1, 4], "with a private window's save, in arrival order");
        const v = Asks.view(pw, "github.com");
        compare(v.lines.map(l => l.text), ["Save password for github.com?"], "PLAN.md 7's wording");
        compare(v.actions.map(a => a.label), ["Save", "Not now"], "text actions on its line: Save · Not now");
        compare(v.buttons, [], "no buttons: a line, not the ask's boxes");
        compare(v.negative, "notnow", "Esc is Not now");
        compare(Asks.view(external, "github.com").actions, [], "the other asks keep their buttons");
    }

    function state(passwords) {
        return {
            engine: "duckduckgo", defaultBrowser: "yes", folder: "/home/abdul/Downloads", home: "/home/abdul", zoom: 1,
            fonts: { standard: "Noto Serif", size: 16 }, minimumFont: 0, languages: ["en-GB"], spellCheck: false, dictionaries: {},
            allowed: [], siteChoices: [], lists: [], updating: false, listFailure: null, thirdPartyCookies: "allow",
            httpsFirst: "on", cookies: 0, clearing: null, extensions: [], versions: {}, now: Date.now(),
            passwords: Object.assign({ state: "ready", entries: entries, removing: {}, revealed: {} }, passwords ?? {}),
        };
    }
    function section(rows) {
        const at = rows.findIndex(r => r.key === "head:passwords"), next = rows.findIndex((r, i) => i > at && r.kind === "head");
        return at < 0 ? [] : rows.slice(at + 1, next < 0 ? rows.length : next);
    }

    function test_settingsListsThemInAViewInPlace() {
        const main = section(Settings.rows(state(), "", ""));
        compare(main.map(r => [r.kind, r.label, r.value ?? ""]), [["link", "Saved passwords", "3"],
            ["note", "Thyris doesn't fill in addresses or card numbers, and never stores card numbers.", ""]],
            "the count and ›, then PLAN.md 3.15's gap in its words");
        compare(section(Settings.rows(state(), "", "router")).map(r => r.key), ["passwords"], "the page's filter finds a login by host");
        compare(section(Settings.rows(state(), "", "work")).map(r => r.key), ["passwords"], "and by user name");
        const view = Settings.rows(state(), "passwords", "");
        compare(view.map(r => [r.kind, r.label, r.value ?? ""]), [["back", "Saved passwords", ""], ["login", "github.com", "abdul"],
                                                                ["login", "github.com", "work"], ["login", "http://router.lan", ""]]);
        compare(Settings.rows(state(), "passwords", "work").map(r => r.value ?? ""), ["", "work"], "the filter, in the view too");
        const empty = Settings.rows(state({ entries: [] }), "passwords", "");
        compare(empty[1].label, "None yet. When you sign in to a site, Thyris asks to save the password.");
        compare(section(Settings.rows(state({ entries: [] }), "", ""))[0].value, "0");
    }

    function test_showDeleteAndNoKeyring() {
        const id = entries[0].id;
        const shown = Settings.rows(state({ revealed: { [id]: { secret: "hunter2" } } }), "passwords", "");
        compare(shown[1].shown, { secret: "hunter2" }, "Show's password goes to its row");
        verify(shown.every(r => (r.words ?? "").indexOf("hunter2") < 0 && String(r.value ?? "").indexOf("hunter2") < 0), "never into what the filter reads");
        compare(Settings.rows(state({ revealed: { [id]: { secret: "hunter2" } } }), "passwords", "hunter2").length, 1, "so it finds nothing");
        const deleting = Settings.rows(state({ removing: { [id]: 5 } }), "passwords", "");
        compare([deleting[1].kind, deleting[1].label, deleting[1].value, deleting[1].action], ["action", "github.com", "Deleted in 5 s", "Undo"],
                "a deleted one waits for Undo, as a clear does");
        compare(deleting[1].key, "undelete:" + id);
        const none = "Passwords can't be saved: no password store is running (such as GNOME Keyring or KWallet).";
        compare(section(Settings.rows(state({ state: "none", entries: [] }), "", "")).map(r => r.label)[0], none);
        compare(Settings.rows(state({ state: "none", entries: [] }), "passwords", "")[1].label, none);
    }
}
