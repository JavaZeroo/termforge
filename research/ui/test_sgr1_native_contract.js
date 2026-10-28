'use strict';
// Exercise the unchanged native boundary assertions against this case's generated body.
const fs = require('fs'), path = require('path'), vm = require('vm'), assert = require('assert');
const generated = fs.readFileSync(process.argv[2], 'utf8');
const start = generated.indexOf('const started=Date.now(),captures=[],grabs=[],targets=');
const end = generated.indexOf(';\nfunction modulePathMatches', start);
assert(start >= 0 && end > start);
const descriptors = vm.runInNewContext('(function(){' + generated.slice(start, end) + ';return targets;})()', {Date});
assert.strictEqual(JSON.stringify(descriptors.map(row => row.markers.length)), '[4,6,6,6,6]');
for (let stage = 0; stage < descriptors.length; ++stage) {
    const groups = descriptors[stage].markers.reduce((result, row) => {
        (result[row.group] ||= []).push(row.sample); return result;
    }, {});
    assert.strictEqual(JSON.stringify(groups['control-normal']), '[0,1]');
    assert.strictEqual(JSON.stringify(groups['control-sgr1']), '[0,1]');
    if (stage) assert.strictEqual(JSON.stringify(groups.sample), '[0,1]');
}
const template = path.join(__dirname, 'raster_raw_reference.js');
const contract = fs.readFileSync(path.join(__dirname, 'test_raster_raw_native.js'), 'utf8');
function boundaryRequire(name) {
    if (name !== 'fs') return require(name);
    return {...fs, readFileSync(file, ...args) {
        return path.resolve(file) === template ? generated : fs.readFileSync(file, ...args);
    }};
}
vm.runInNewContext(contract, {require: boundaryRequire, __dirname, process}, {filename: 'reviewed-raw-native-contract'});
