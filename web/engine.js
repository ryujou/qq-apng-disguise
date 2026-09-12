import UPNG from 'upng-js';
import { parseGIF, decompressFrame } from 'gifuct-js';
import { deflate } from 'pako';

const signature = new Uint8Array([137,80,78,71,13,10,26,10]);
const ascii = new TextEncoder();
const pause = () => new Promise(resolve => setTimeout(resolve, 0));
const uint = (b, i) => new DataView(b.buffer,b.byteOffset,b.byteLength).getUint32(i);
function u32(n) { const b=new Uint8Array(4);new DataView(b.buffer).setUint32(0,n);return b; }
function join(parts) { const out=new Uint8Array(parts.reduce((n,p)=>n+p.length,0));let i=0;for(const p of parts){out.set(p,i);i+=p.length;}return out; }
const crcTable = new Uint32Array(256).map((_,n)=>{for(let k=0;k<8;k++)n=(n>>>1)^((n&1)?0xedb88320:0);return n>>>0;});
export function chunk(type,data) {
  const body=join([ascii.encode(type),data]);let crc=0xffffffff;
  for(const b of body)crc=crcTable[(crc^b)&255]^(crc>>>8);
  return join([u32(data.length),body,u32((crc^0xffffffff)>>>0)]);
}
function pngChunks(bytes) {
  const result=[];
  for(let i=8;i+12<=bytes.length;) {
    const size=uint(bytes,i);if(size>bytes.length-i-12)throw Error('PNG 文件不完整。');
    const type=String.fromCharCode(...bytes.subarray(i+4,i+8));
    result.push({type,data:bytes.subarray(i+8,i+8+size)});i+=12+size;
    if(type==='IEND')break;
  }
  return result;
}
function canvasPixels(width,height) {
  if(!width||!height||width*height*4>512*1024*1024)throw Error('图片尺寸过大，请缩小后再试。');
  return {width,height,data:new Uint8Array(width*height*4)};
}
function same(a,b) { if(a.length!==b.length)return false;for(let i=0;i<a.length;i++)if(a[i]!==b[i])return false;return true; }
function append(frames,image,ticks,denominator,merge=true) {
  if(!ticks){ticks=1;denominator=10;}
  const last=frames.at(-1);
  if(merge&&last&&last.denominator===denominator&&same(last.data,image.data))last.ticks+=ticks;
  else {
    if((frames.length+1)*image.data.length>512*1024*1024)throw Error('动画解码后超过 512 MB，请减少帧数或缩小图片。');
    frames.push({...image,data:image.data.slice(),ticks,denominator});
  }
}
function clear(image,x,y,w,h,color=[0,0,0,0]) {
  for(let row=y;row<y+h;row++)for(let col=x;col<x+w;col++)image.data.set(color,(row*image.width+col)*4);
}
function composite(image,patch,x,y,over) {
  if(x<0||y<0||x+patch.width>image.width||y+patch.height>image.height)throw Error('动画帧超出画布。');
  for(let row=0;row<patch.height;row++)for(let col=0;col<patch.width;col++) {
    const s=(row*patch.width+col)*4,d=((y+row)*image.width+x+col)*4,alpha=patch.data[s+3];
    if(!over||alpha===255)image.data.set(patch.data.subarray(s,s+4),d);
    else if(alpha) {
      const a=alpha*255+image.data[d+3]*(255-alpha);
      for(let c=0;c<3;c++)image.data[d+c]=Math.floor((patch.data[s+c]*alpha*255+image.data[d+c]*image.data[d+3]*(255-alpha)+a/2)/a);
      image.data[d+3]=Math.floor((a+127)/255);
    }
  }
}
function decodePNG(bytes) {
  const decoded=UPNG.decode(bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.byteLength));
  return {width:decoded.width,height:decoded.height,data:new Uint8Array(UPNG.toRGBA8(decoded)[0])};
}
async function apng(bytes,coverOnly,preserve) {
  const chunks=pngChunks(bytes),header=chunks[0]?.data;
  if(chunks[0]?.type!=='IHDR'||header.length!==13)throw Error('PNG 文件头无效。');
  const image=canvasPixels(uint(header,0),uint(header,4));
  const palette=chunks.filter(c=>c.type==='PLTE'||c.type==='tRNS');
  const animated=chunks.find(c=>c.type==='acTL');
  if(!animated||coverOnly) {
    const png=join([signature,...chunks.filter(c=>['IHDR','PLTE','tRNS','IDAT','IEND'].includes(c.type)).map(c=>chunk(c.type,c.data))]);
    return {frames:[{...decodePNG(png),ticks:100,denominator:1000}],animated:false};
  }
  let ctl=null,parts=[],count=0;const frames=[];
  async function finish() {
    if(!ctl)return;
    if(!parts.length)throw Error('动画帧缺少数据。');
    const w=uint(ctl,4),h=uint(ctl,8),x=uint(ctl,12),y=uint(ctl,16);
    const ihdr=join([u32(w),u32(h),header.subarray(8)]);
    const png=join([signature,chunk('IHDR',ihdr),...palette.map(c=>chunk(c.type,c.data)),chunk('IDAT',join(parts)),chunk('IEND',new Uint8Array())]);
    const patch=decodePNG(png),disposal=ctl[24],blend=ctl[25];
    if(disposal>2||blend>1)throw Error('动画帧操作无效。');
    const previous=disposal===2?image.data.slice():null;
    composite(image,patch,x,y,blend===1);
    append(frames,image,ctl[20]*256+ctl[21],ctl[22]*256+ctl[23]||100,preserve);
    if(disposal===1)clear(image,x,y,w,h);else if(disposal===2)image.data=previous;
    parts=[];count++;await pause();
  }
  for(const c of chunks) {
    if(c.type==='fcTL'){await finish();if(c.data.length!==26)throw Error('动画控制帧无效。');ctl=c.data;}
    else if(c.type==='fdAT'){if(!ctl||c.data.length<4)throw Error('动画帧无效。');parts.push(c.data.subarray(4));}
    else if(c.type==='IDAT'&&ctl)parts.push(c.data);
  }
  await finish();if(count!==uint(animated.data,0))throw Error('动画帧数与文件不一致。');
  return {frames,animated:true};
}
async function gif(bytes,coverOnly,preserve) {
  const parsed=parseGIF(bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.byteLength));
  const image=canvasPixels(parsed.lsd.width,parsed.lsd.height),frames=[];
  const rawFrames=parsed.frames.filter(f=>f.image);
  for(let i=0;i<rawFrames.length;i++) {
    const raw=rawFrames[i],f=decompressFrame(raw,parsed.gct,true),{left:x,top:y,width:w,height:h}=f.dims;
    const bg=f.transparentIndex!==undefined?[0,0,0,0]:[...(parsed.gct?.[parsed.lsd.backgroundColorIndex]||[0,0,0]),255];
    if(!i)clear(image,0,0,image.width,image.height,bg);
    const previous=f.disposalType===3?image.data.slice():null;
    composite(image,{width:w,height:h,data:f.patch},x,y,true);
    append(frames,image,raw.gce?.delay||10,100,preserve);
    if(coverOnly)break;
    if(f.disposalType===2)clear(image,x,y,w,h,bg);else if(f.disposalType===3)image.data=previous;
    await pause();
  }
  if(!frames.length)throw Error('GIF 没有可用的画面。');
  return {frames,animated:!coverOnly&&rawFrames.length>1};
}
export async function decodeAsset(file,coverOnly=false,preserve=true) {
  const bytes=new Uint8Array(await file.arrayBuffer());
  if(signature.every((n,i)=>bytes[i]===n))return apng(bytes,coverOnly,preserve);
  if(String.fromCharCode(...bytes.subarray(0,3))==='GIF')return gif(bytes,coverOnly,preserve);
  const bitmap=await createImageBitmap(file);
  try {
    canvasPixels(bitmap.width,bitmap.height);
    const canvas=document.createElement('canvas');canvas.width=bitmap.width;canvas.height=bitmap.height;
    const ctx=canvas.getContext('2d');ctx.drawImage(bitmap,0,0);
    return {frames:[{width:canvas.width,height:canvas.height,data:new Uint8Array(ctx.getImageData(0,0,canvas.width,canvas.height).data),ticks:100,denominator:1000}],animated:false};
  } finally { bitmap.close(); }
}
export function fitImage(image,width,height) {
  if(image.width===width&&image.height===height)return image;
  const source=document.createElement('canvas');source.width=image.width;source.height=image.height;
  source.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(image.data),image.width,image.height),0,0);
  const target=document.createElement('canvas');target.width=width;target.height=height;
  const ctx=target.getContext('2d');ctx.fillStyle='white';ctx.fillRect(0,0,width,height);
  const scale=Math.min(width/image.width,height/image.height),w=Math.max(1,Math.round(image.width*scale)),h=Math.max(1,Math.round(image.height*scale));
  ctx.clearRect(Math.floor((width-w)/2),Math.floor((height-h)/2),w,h);
  ctx.drawImage(source,Math.floor((width-w)/2),Math.floor((height-h)/2),w,h);
  return {width,height,data:new Uint8Array(ctx.getImageData(0,0,width,height).data)};
}
function pngData(image) {
  const stride=image.width*4,raw=new Uint8Array((stride+1)*image.height);
  for(let y=0;y<image.height;y++) {
    const offset=y*(stride+1);raw[offset]=1;
    for(let x=0;x<stride;x++)raw[offset+1+x]=(image.data[y*stride+x]-(x>=4?image.data[y*stride+x-4]:0))&255;
  }
  return deflate(raw,{level:6});
}
export async function encodeDisguise(cover,frames,onProgress=()=>{}) {
  if(!frames.length)throw Error('请先添加播放图片或动画。');
  const {width,height}=frames[0];let count=0;
  const timing=frames.map(f=>{
    let {ticks,denominator}=f;if(denominator<10){ticks*=10;denominator*=10;}
    const step=Math.max(1,Math.floor(denominator/10));count+=Math.ceil(ticks/step);
    return {ticks,denominator,step};
  });
  const splitSingle=count===1&&(timing[0].ticks>1||timing[0].denominator<=32767);if(splitSingle)count=2;
  if(count>0xffffffff)throw Error('动画帧数过多。');
  const out=[signature,chunk('IHDR',join([u32(width),u32(height),new Uint8Array([8,6,0,0,0])])),chunk('acTL',join([u32(count),u32(0)]))];
  out.push(chunk('tEXt',ascii.encode(`ChatBarApngDisguise\0${frames.length>1?'1;ANIMATED;'+count:'1;STATIC;1'}`)));
  out.push(chunk('IDAT',pngData(fitImage(cover,width,height))));let sequence=0;
  function control(w,h,ticks,denominator) {
    if(denominator===1000&&ticks%10===0){ticks/=10;denominator=100;}
    const data=join([u32(sequence++),u32(w),u32(h),u32(0),u32(0),new Uint8Array([ticks>>8,ticks&255,denominator>>8,denominator&255,0,0])]);
    out.push(chunk('fcTL',data));
  }
  function frameData(data) { for(let i=0;i<data.length;i+=65536)out.push(chunk('fdAT',join([u32(sequence++),data.subarray(i,i+65536)]))); }
  for(let i=0;i<frames.length;i++) {
    onProgress(i+1,frames.length);await pause();
    const image=fitImage(frames[i],width,height);let {ticks,denominator,step}=timing[i];
    if(splitSingle){if(ticks===1){ticks*=2;denominator*=2;}step=Math.floor(ticks/2);}
    const first=Math.min(step,ticks);control(width,height,first,denominator);frameData(pngData(image));
    const hold=pngData({width:1,height:1,data:image.data.subarray(0,4)});
    for(let remaining=ticks-first;remaining>0;){const n=splitSingle?remaining:Math.min(step,remaining);control(1,1,n,denominator);frameData(hold);remaining-=n;}
  }
  out.push(chunk('IEND',new Uint8Array()));return new Blob(out,{type:'image/png'});
}
