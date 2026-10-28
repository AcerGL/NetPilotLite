// Developer-only offline transformer: reads stdin JSON, returns candidate JSON; never talks to Clash.
const {nplTransform}=require('./transform.js');let input='';process.stdin.setEncoding('utf8');process.stdin.on('data',x=>input+=x);process.stdin.on('end',()=>{const x=JSON.parse(input);process.stdout.write(JSON.stringify(nplTransform(x.config,x.policy)));});
