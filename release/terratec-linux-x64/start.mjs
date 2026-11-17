import {makeServer} from './server/index.mjs';
import {fileURLToPath} from 'node:url';
const port=Number(process.env.PORT || 3001);
if(!Number.isInteger(port)||port<1||port>65535) throw new Error('PORT must be an integer from 1 to 65535');
makeServer({dataDir:process.env.TERRATEC_DATA_DIR,staticDir:fileURLToPath(new URL('./dist/',import.meta.url))}).listen(port,'127.0.0.1',()=>console.log('TerraTec: http://127.0.0.1:'+port));
