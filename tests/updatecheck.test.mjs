// node tests/updatecheck.test.mjs : tests js/UpdateCheck.js (the QML library) outside Qt.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
const src = readFileSync(new URL('../js/UpdateCheck.js', import.meta.url), 'utf8').replace(/^\.pragma library$/m, '');
const U = new Function(src + '\nreturn { parseVersion, isNewer, isRequired, newerRelease };')();
assert.deepEqual(U.parseVersion('v0.2.8'), [0, 2, 8]);
assert.equal(U.parseVersion('dev'), null);
assert.ok(U.isNewer('v0.2.8', '0.2.7'));
assert.ok(U.isNewer('0.10.0', '0.9.9'));
assert.ok(!U.isNewer('0.2.8', '0.2.8'));
assert.ok(!U.isNewer('v9.9.9', 'dev'));
assert.ok(U.isRequired('x\r\nUpdate: required\r\ny'));
assert.ok(!U.isRequired('an update: required reading'));
const rel = (b) => JSON.stringify({ tag_name: 'v0.3.0', html_url: 'https://x/r', body: b });
assert.deepEqual(U.newerRelease(rel('Update: required'), '0.2.9'), { version: '0.3.0', url: 'https://x/r', required: true });
assert.equal(U.newerRelease(rel(''), '0.2.9').required, false);
assert.equal(U.newerRelease(rel(''), '0.3.0'), null);
assert.equal(U.newerRelease('not json', '0.1.0'), null);
console.log('updatecheck ok');
