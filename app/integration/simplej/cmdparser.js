const commandLineArgs = require('command-line-args');

// simpleje  --port(p) number  --command(c) cmd --topic(t) topic
// port defaults to 1883
// command defaults to: send "async", "sync" is the other option
// topic defaults to "/mach/func/request"
module.exports = new function parseArgs() {

    const optdefs = [
        { name: 'port', alias: 'p', type: String, defaultValue: '1883'},
        { name: 'command', alias: 'c', type: String, defaultValue: 'async'},       
        { name: 'topic', alias: 't', type: String, defaultValue: '/app-1/mach/func/request'}                
    ];
    var options;
    try {
        options = commandLineArgs(optdefs);

    } catch(e) {
        console.log(e.name);
        // there was an error.. what is it?
        process.exit(1);
    }

    return options;
};
