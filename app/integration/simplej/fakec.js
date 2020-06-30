'use strict'

const JAMP = require('./jamprotocol');
const mqtt = require('mqtt');
const mqttconsts = require('./constants').mqtt;
const cmdparser = require('./cmdparser');
const cbor = require('cbor');



var copts = {
    clientId: 'fakeC',
    keepalive: mqttconsts.keepAlive,
    clean: false,
    connectTimeout: mqttconsts.connectionTimeout,
};

var mserv = mqtt.connect("tcp://localhost:" + cmdparser.port, copts);

mserv.subscribe(cmdparser.app + '/replies/down');
mserv.subscribe(cmdparser.app + '/requests/up');



setInterval(function () {
    switch (cmdparser.command) {
        case 'sync':
            console.log("Sending sync command...");
            var req = JAMP.createRemoteSyncReq("testsync", [1, 2], "", 0, "device", 1, 1);
            mserv.publish('/' + cmdparser.app + '/requests/up', cbor.encode(JSON.stringify(req)));
        break;
        case 'async':
            console.log("Sending async command...");
            var req = JAMP.createRemoteAsyncReq("testasync", [1, 2], "", 0, "device", 1, 1);
	        console.log(JSON.stringify(req));
            mserv.publish('/' + cmdparser.app + '/requests/up', cbor.encode(JSON.stringify(req)));  
        break;
    }
}, 300);
