import { defineConfig } from 'vite';
import vue from '@vitejs/plugin-vue';
const proxy={ '/api': 'http://127.0.0.1:3001' };
export default defineConfig({
 plugins:[vue()],server:{proxy},preview:{host:'127.0.0.1',proxy},
 build:{manifest:true,rolldownOptions:{output:{codeSplitting:{groups:[
  {name:'three-core',test:/node_modules[\\/]three[\\/]build[\\/]three\.core\.js$/,priority:30},
  {name:'three-renderer',test:/node_modules[\\/]three[\\/]build[\\/]three\.module\.js$/,priority:20},
  {name:'three-addons',test:/node_modules[\\/]three[\\/]examples[\\/]/,priority:10},
  {name:'vue-runtime',test:/node_modules[\\/](?:@vue|vue)[\\/]/,priority:10}
 ]}}}}
});
