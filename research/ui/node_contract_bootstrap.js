'use strict';
// Synchronous checkpoints survive an event-loop hang and precede test execution.
const fs=require('fs');
const mode=process.argv[2];
if(!['normal','hang-vm-helpers','throw-before-grab'].includes(mode))throw Error('Unknown diagnostic case');
let sequence=0;
function observe(stage){
    const row={sequence:sequence++,stage,pid:process.pid,monotonic_ns:process.hrtime.bigint().toString()};
    if(stage==='node-first-checkpoint')Object.assign(row,{node_version:process.version,arch:process.arch,platform:process.platform});
    const bytes=Buffer.from('TF_NODE_CONTRACT_V1:'+JSON.stringify(row)+'\n');
    let offset=0;
    while(offset<bytes.length){const count=fs.writeSync(2,bytes,offset,bytes.length-offset);if(!count)throw Error('Checkpoint write failed');offset+=count;}
    if(mode==='hang-vm-helpers'&&stage==='vm-helpers-start')Atomics.wait(new Int32Array(new SharedArrayBuffer(4)),0,0);
    if(mode==='throw-before-grab'&&stage==='case-start:grab')throw Error('TF_NODE_CONTROLLED_EXCEPTION');
}
observe('node-first-checkpoint');
const gate=Buffer.alloc(3);
let received=0;
while(received<gate.length){const count=fs.readSync(0,gate,received,gate.length-received,null);if(!count)throw Error('Owned GO gate closed');received+=count;}
if(gate.toString('ascii')!=='GO\n')throw Error('Invalid owned GO gate');
observe('go-received');
global.__termforgeNodeContractObserve=observe;
observe('fixture-before-load');
require('./test_raster_native_contract.js');
