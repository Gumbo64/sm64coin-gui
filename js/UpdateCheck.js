.pragma library
// SM64COIN: "update available" notice. Pure functions (tests/updatecheck.test.mjs runs them in node).
// A release marks an update as required with a line `Update: required` in its body.

var LATEST_URL = "https://api.github.com/repos/Gumbo64/sm64coin/releases/latest"
var CACHE_MS = 6 * 3600 * 1000

// "v0.2.8" / "0.2.8-rc1" -> [0, 2, 8]; null if not a release version ("dev").
function parseVersion(s) {
    var m = /^v?(\d+)\.(\d+)\.(\d+)/.exec(String(s === undefined || s === null ? "" : s).trim())
    return m ? [parseInt(m[1]), parseInt(m[2]), parseInt(m[3])] : null
}

function isNewer(latest, current) {
    var a = parseVersion(latest), b = parseVersion(current)
    if (!a || !b) return false
    for (var i = 0; i < 3; i++)
        if (a[i] !== b[i]) return a[i] > b[i]
    return false
}

function isRequired(body) {
    return /^\s*Update:\s*required\s*$/im.test(String(body === undefined || body === null ? "" : body))
}

// GitHub release JSON text -> { version, url, required } if it is newer than `current`, else null.
function newerRelease(jsonText, current) {
    var rel
    try { rel = JSON.parse(jsonText) } catch (e) { return null }
    var v = parseVersion(rel && rel.tag_name)
    if (!v || !isNewer(rel.tag_name, current)) return null
    return {
        version: v.join("."),
        url: rel.html_url || "https://github.com/Gumbo64/sm64coin/releases/latest",
        required: isRequired(rel.body)
    }
}
