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

var registry = new Map();
registry['hellofunc'] = function (x) {
    console.log("Say hello ", x);
}

registry['gethello'] = function (x) {
    return "good morning, " + x;
}

registry['addNumbers'] = function (x) {
    console.log("Numbers ", x, 20);
    return x + 20;
}

var mserv = mqtt.connect("tcp://localhost:" + cmdparser.port, copts);

mserv.subscribe('/' + cmdparser.app + '/replies/up');
mserv.subscribe('/' + cmdparser.app + '/requests/up');


function runAsyncCallback(cmsg, callback) {
    console.log("ASYNC Executing... ", cmsg.actname);
    registry[cmsg.actname](cmsg.args[0]);
    cmsg.cmd = "REXEC-ACK";
    callback(cmsg);
}

function runSyncCallback(cmsg, callback) {
    console.log("SYNC Executing... ", cmsg.actname);    
    cmsg.cmd = "REXEC-ACK";
    callback('first', cmsg);
    setTimeout(callback, 2, 'second', cmsg);
}

mserv.on('message', function(topic, buf) {    
    cbor.decodeFirst(buf, function(error, msg) {
        var cmsg = JSON.parse(msg);
        switch (topic) {
            case '/' + cmdparser.app + '/replies/up':
                if (cmsg.cmd == "REXEC-ACK") {
                    console.log("ACK Received .. msg.actid = ", cmsg.actid);                    
                } else if (cmsg.cmd == "REXEC-RES") {
                    console.log("RES Received .. value =", cmsg.args);
                }
            break;
            case '/' + cmdparser.app + '/requests/up':
                if (cmsg.cmd == "REXEC-ASY") {
                    runAsyncCallback(cmsg, function(smsg) {
                        mserv.publish('/' + cmdparser.app + '/replies/down', cbor.encode(JSON.stringify(smsg)));
                    });
                } else if (cmsg.cmd == "REXEC-SYN") {
                    runSyncCallback(cmsg, function(step, smsg) {
                        switch (step) {
                            case 'first':
                                mserv.publish('/' + cmdparser.app + '/replies/down', cbor.encode(JSON.stringify(smsg)));
                            break;
                            case 'second':
                                smsg.cmd = "REXEC-RES";
                                smsg.args = registry[smsg.actname](smsg.args[0]);
                                console.log("Writng.... ", smsg.args);
                                mserv.publish('/' + cmdparser.app + '/replies/down', cbor.encode(JSON.stringify(smsg)));
                            break;
                        }
                    });
                }
            break;
        }
    });
});


setInterval(function () {
    switch (cmdparser.command) {
        case 'sync':
            console.log("Sending sync command...");
            var req = JAMP.createRemoteSyncReq("RPCFunctionJSync", [1, 2], "", 0, "device", 1, 1);
            mserv.publish('/' + cmdparser.app + '/requests/down', cbor.encode(JSON.stringify(req)));
        break;
        case 'async':
            console.log("Sending async command...");
            var req = JAMP.createRemoteAsyncReq("RPCFunctionJAsync", [1, 2], "", 0, "device", 1, 1);
            mserv.publish('/' + cmdparser.app + '/requests/down/c', cbor.encode(JSON.stringify(req)));
            var req = JAMP.createRemoteAsyncReq("addNumbers", [10, 25], "", 0, "device", 1, 1);
            mserv.publish('/' + cmdparser.app + '/requests/down/c', cbor.encode(JSON.stringify(req)));
	break;
	case 'strdup':
	    console.log("Sending strdup command...");
            var req = JAMP.createRemoteAsyncReq("DuplicateCString", ["command.c, args_t, combo_ptr"], "", 0, "device", 1, 1);
            mserv.publish('/' + cmdparser.app + '/requests/down', cbor.encode(JSON.stringify(req)));
        break;
    }
}, 300);
