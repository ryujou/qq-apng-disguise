import {build} from 'esbuild';
import {mkdir,copyFile,readFile,writeFile} from 'node:fs/promises';
await mkdir('dist',{recursive:true});
await build({entryPoints:['app.js'],bundle:true,minify:true,format:'esm',outfile:'dist/app.js',legalComments:'eof'});
for(const file of ['index.html','style.css','icon.png'])await copyFile(file,'dist/'+file);
const packages=['gifuct-js','js-binary-schema-parser','upng-js','pako'];
let licenses='Third-party licenses\n';
for(const name of packages){const root='node_modules/'+name+'/';const pkg=JSON.parse(await readFile(root+'package.json','utf8'));licenses+='\n===== '+name+' '+pkg.version+' =====\n'+await readFile(root+(name==='pako'?'LICENSE':'LICENSE'), 'utf8');}
await writeFile('dist/licenses.txt',licenses);
