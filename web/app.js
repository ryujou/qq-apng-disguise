import {decodeAsset,encodeDisguise} from './engine.js';
const $=id=>document.getElementById(id);
let coverFile=null,files=[],coverURL=null,resultURL=null;
const status=(message,error=false)=>{$('status').textContent=message;$('status').classList.toggle('error',error);};
function invalidate(){if(resultURL){URL.revokeObjectURL(resultURL);resultURL=null;}$('result-preview').hidden=true;$('result-preview').removeAttribute('src');$('preview-empty').hidden=false;$('save-result').hidden=true;status('图片仅在当前浏览器处理。');}
function setCover(file){if(!file)return;coverFile=file;invalidate();if(coverURL)URL.revokeObjectURL(coverURL);coverURL=URL.createObjectURL(file);$('cover-thumb').src=coverURL;$('cover-thumb').hidden=false;$('cover-drop').querySelector('svg').setAttribute('hidden','');$('cover-label').textContent=file.name;}
function renderFiles(){const list=$('file-list');list.replaceChildren();files.forEach((file,index)=>{const row=document.createElement('li');row.className='file-row';const number=document.createElement('span');number.className='file-index';number.textContent=String(index+1).padStart(2,'0');const name=document.createElement('span');name.className='file-name';name.textContent=file.name;const size=document.createElement('span');size.className='file-size';size.textContent=(file.size/1024/1024).toFixed(1)+' MB';const remove=document.createElement('button');remove.type='button';remove.className='remove';remove.textContent='移除';remove.setAttribute('aria-label','移除 '+file.name);remove.onclick=()=>{files.splice(index,1);invalidate();renderFiles();};row.append(number,name,size,remove);list.append(row);});}
function addFiles(selected){files.push(...selected);invalidate();renderFiles();}
function dropZone(id,input,onFiles){const zone=$(id);zone.onclick=()=>$(input).click();$(input).onchange=e=>{onFiles([...e.target.files]);e.target.value='';};for(const type of ['dragenter','dragover'])zone.addEventListener(type,e=>{e.preventDefault();if(!$('editor').disabled)zone.classList.add('dragover');});zone.addEventListener('dragleave',()=>zone.classList.remove('dragover'));zone.addEventListener('drop',e=>{e.preventDefault();zone.classList.remove('dragover');if(!$('editor').disabled)onFiles([...e.dataTransfer.files]);});}
dropZone('cover-drop','cover',selected=>{if(selected.length>1){status('封面请一次选择一张图片。',true);return;}setCover(selected[0]);});
dropZone('playback-drop','playback',addFiles);
for(const id of ['delay','preserve'])$(id).addEventListener('change',invalidate);
$('generate').onclick=async()=>{
  if(!coverFile){status('请先选择封面图片。',true);$('cover-drop').focus();return;}
  if(!files.length){status('请添加播放图片、GIF 或 APNG。',true);$('playback-drop').focus();return;}
  if(!$('delay').reportValidity())return;
  const delay=Number($('delay').value),preserve=$('preserve').checked;
  $('editor').disabled=true;$('generate').disabled=true;
  try {
    invalidate();status('正在读取封面…');
    const cover=(await decodeAsset(coverFile,true)).frames[0];const frames=[];let memory=0;
    for(let i=0;i<files.length;i++){
      status(`正在解码 ${i+1}/${files.length}：${files[i].name}`);
      const decoded=await decodeAsset(files[i],false,preserve);
      for(const frame of decoded.frames){if(!decoded.animated||!preserve){frame.ticks=delay;frame.denominator=1000;}memory+=frame.data.byteLength;if(memory>512*1024*1024)throw Error('播放图片解码后超过 512 MB，请减少图片或缩小分辨率。');frames.push(frame);}
    }
    const total=frames.reduce((n,f)=>n+f.ticks/f.denominator,0);
    const blob=await encodeDisguise(cover,frames,(i,n)=>status(`正在生成 ${i}/${n} 个画面…`));
    resultURL=URL.createObjectURL(blob);$('result-preview').src=resultURL;$('result-preview').hidden=false;$('preview-empty').hidden=true;
    const link=$('save-result');link.href=resultURL;link.download=coverFile.name.replace(/\.[^.]+$/,'')+'_藏图.png';link.hidden=false;
    status(`已生成 · ${frames.length} 个画面 · 一轮 ${Number(total.toFixed(3))} 秒 · ${(blob.size/1024/1024).toFixed(1)} MB`);
    link.click();
  }catch(error){status(error.message||String(error)||'处理失败，请检查图片格式。',true);}
  finally{$('editor').disabled=false;$('generate').disabled=false;}
};
