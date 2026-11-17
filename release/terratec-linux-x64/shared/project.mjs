import {validateFurniturePresets} from './furniture-library.mjs';
import {validateWorkshopProfiles} from './workshop-profiles.mjs';
import { rectangle, bounds, validateOutline } from './geometry.mjs';
import { validateOpenings } from './openings.mjs';
import { validatePresets } from './presets.mjs';
import { validateFurniture } from './furniture.mjs';
import { validateWalls, usesDefaultWalls } from './room-topology.mjs';
import { validateDesigns } from './furniture-design.mjs';
import { syncPlacedDesign } from './design-placement.mjs';
export function validateProject(value) {
  if (!value || ![1,2,3,4,5,6,7].includes(value.schemaVersion) || value.units !== 'm')
    throw new Error('Expected schemaVersion 1, 2, 3, 4, 5, 6 or 7 and units m');
  if (typeof value.name !== 'string' || !value.name.trim() || value.name.length > 120)
    throw new Error('Project name must contain 1–120 characters');
  const dimension=v=>typeof v==='number' && Number.isFinite(v) && v>=.1 && v<=1000;
  if (!value.room || !dimension(value.room.height)) throw Error('Height must be between 0.1 and 1000 metres');
  if(value.schemaVersion===1 && ![value.room.width,value.room.depth].every(dimension))
    throw Error('Room dimensions must be numbers between 0.1 and 1000 metres');
  const outline=validateOutline(value.schemaVersion===1 ? rectangle(value.room.width,value.room.depth) : value.room.outline);
  if(value.schemaVersion<7 && outline.length!==4) throw Error('Schema 1–6 rooms need four corners');
  // Never reinterpret stored wall IDs as the original four: that would silently rename walls.
  if(value.schemaVersion<7 && value.room.walls!==undefined && !usesDefaultWalls(value.room.walls)) throw Error('Stored wall lists require schemaVersion 7');
  if(value.schemaVersion<7 && value.room.locked===true) throw Error('Locked rooms require schemaVersion 7');
  const locked=value.schemaVersion===7 && value.room.locked!==undefined ? value.room.locked : false;
  if(typeof locked!=='boolean') throw Error('Room lock must be true or false');
  // Furniture designs are schema 7 project data; an empty list is not stored.
  if(value.schemaVersion<7 && value.furnitureDesigns?.length) throw Error('Furniture designs require schemaVersion 7');
  const designs=value.schemaVersion===7 && value.furnitureDesigns!==undefined ? validateDesigns(value.furnitureDesigns) : [];
  // Schema 7 stores wall IDs; rooms with the original four walls are written as schema 6.
  const walls=validateWalls(value.schemaVersion===7?value.room.walls:undefined,outline.length);
  const {width,depth}=bounds(outline);
  const openings=validateOpenings(value.schemaVersion>=3?value.room.openings:[],{outline,walls},value.room.height);
  const presets=validatePresets(value.schemaVersion>=5?value.presets:[]);
  // Placed designs take their size from the design they refer to.
  const designsById=new Map(designs.map(d=>[d.id,d])),rawFurniture=value.schemaVersion>=6?value.room.furniture:[];
  const furniture=validateFurniture(Array.isArray(rawFurniture)?rawFurniture.map(item=>item?.type==='design'?syncPlacedDesign(item,designsById):item):rawFurniture,outline,value.room.height);
  return {...(value.furniturePresets!==undefined?{furniturePresets:validateFurniturePresets(value.furniturePresets)}:{}),schemaVersion:walls||locked||designs.length?7:6,presets,...(designs.length?{furnitureDesigns:designs}:{}),workshopProfiles:validateWorkshopProfiles(value.workshopProfiles??[]),units:'m',name:value.name.trim(),room:{width,depth,height:value.room.height,outline,...(walls?{walls}:{}),...(locked?{locked}:{}),openings,furniture}};
}
