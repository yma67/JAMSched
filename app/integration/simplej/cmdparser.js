const commandLineArgs = require('command-line-args');

// simpleje  --port(p) number  --command(c) cmd --app(a) appname
// port defaults to 1883
// command defaults to: "none"
// topic defaults to "/mach/func/request"
module.exports = new function parseArgs() {

    const optdefs = [
        { name: 'port', alias: 'p', type: String, defaultValue: '1883'},
        { name: 'command', alias: 'c', type: String, defaultValue: 'none'},       
        { name: 'app', alias: 'a', type: String, defaultValue: 'app-1'}
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
