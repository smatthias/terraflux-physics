import {validateProject} from './project.mjs';
import {clearanceSettings,reviewFurnitureClearance} from './furniture-clearance.mjs';
export function clearanceSnapshot(project,options,generatedAt=new Date().toISOString()){
 const source=validateProject(project),settings=clearanceSettings(options);
 if(typeof generatedAt!=='string'||!Number.isFinite(Date.parse(generatedAt)))throw Error('Expected a valid review timestamp');
 return JSON.parse(JSON.stringify({schemaVersion:1,kind:'terratec-clearance-review',generatedAt,units:'m',project:{name:source.name},settings,review:reviewFurnitureClearance(source.room,settings),room:source.room,scope:'Applied geometry and workspace planning settings at export time. Advisory review, not building-code certification. Handles, door thickness and installation tolerances are excluded.'}));
}
export function clearanceJson(project,options){return JSON.stringify(clearanceSnapshot(project,options),null,2);}
const cell=value=>'"'+String(value??'').replace(/^[=+\-@\t\r]/,"'$&").replace(/"/g,'""')+'"';
export function clearanceCsv(project,options){
 const snapshot=clearanceSnapshot(project,options),{review,settings}=snapshot;
 const rows=[['TerraTec clearance review',snapshot.project.name],['Generated at',snapshot.generatedAt],['Units',snapshot.units],['Minimum wall gap',settings.minWall],['Door access depth',settings.doorDepth],['Cabinet front service depth',settings.serviceDepth],['Preview enforcement',settings.enforce],['Cabinets',review.cabinetCount],['Affected cabinets',review.affectedCount],['Issues',review.issues.length],['Scope',snapshot.scope],[],['Cabinet ID','Cabinet name','Issue code','Wall','Opening ID','Other cabinet ID','Message']];
 for(const issue of review.issues)rows.push([issue.cabinetId,issue.cabinetName,issue.code,issue.wall,issue.openingId,issue.otherCabinetId,issue.message]);
 return rows.map(row=>row.map(cell).join(',')).join('\r\n')+'\r\n';
}
