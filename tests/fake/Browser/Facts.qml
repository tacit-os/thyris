pragma Singleton
import QtQuick

// Stand-in for the C++ Facts (src/facts.h): the TLS handshakes asked for; tests answer with tlsFound. A connection's
// certificate stands in as its text: "" is none, any other its SHA-256.
QtObject {
    property var asked: []
    signal tlsFound(string host, int port, var tls)
    function lookUpTls(host, port) { asked = asked.concat([host + ":" + port]) }
    function connection(der, protocol) { return der === "" ? {} : { sha256: der, protocol: protocol } }
}
