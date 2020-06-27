'use strict'

const JAMP = require('./jamprotocol');
const mqtt = require('mqtt');
const mqttconsts = require('./constants').mqtt;
const cmdparser = require('./cmdparser');
const cbor = require('cbor');



var copts = {
    clientId: 'testClient-simpleJ',
    keepalive: mqttconsts.keepAlive,
    clean: false,
    connectTimeout: mqttconsts.connectionTimeout,
};

var mserv = mqtt.connect("tcp://localhost:" + cmdparser.port, copts);

setInterval(function () {
    switch (cmdparser.command) {
        case 'sync':
            console.log("Sending sync command...");
            var req = JAMP.createRemoteSyncReq("RPCFunctionJSync", [1, 2], "", 0, "device", 1, 1);
            mserv.publish(cmdparser.topic, cbor.encode('{"cmd": "REXEC-SYN", "actname": "RPCFunctionJSync", "args": [1, 2]}'));
        break;
        case 'async':
            console.log("Sending async command...");
            var req = JAMP.createRemoteAsyncReq("RPCFunctionJAsync", [1, 2], "", 0, "device", 1, 1);
            mserv.publish(cmdparser.topic, cbor.encode('{"cmd": "REXEC-ASY", "actname": "RPCFunctionJAsync", "args": [1, 2]}'));
        break;
        case 'strdup':
            mserv.publish(cmdparser.topic, cbor.encode('{"cmd": "REXEC-ASY", "actname": "DuplicateCString", "args": ["command.c is causing memory leaks. "]}'));
        break;
    }
}, 300);
