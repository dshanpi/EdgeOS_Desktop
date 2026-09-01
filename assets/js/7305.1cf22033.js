"use strict";(self.webpackChunkembedded_linux_ui_course_docs=self.webpackChunkembedded_linux_ui_course_docs||[]).push([["7305"],{3509(t,e,a){a.d(e,{diagram:()=>$});var r=a(7454),i=a(5637),l=a(1916),o=a(6980),s=a(1293),n=a(6827),d=a(8731),c=o.UI.packet,k=class{constructor(){this.packet=[],this.setAccTitle=o.SV,this.getAccTitle=o.iN,this.setDiagramTitle=o.ke,this.getDiagramTitle=o.ab,this.getAccDescription=o.m7,this.setAccDescription=o.EI}static{(0,n.K)(this,"PacketDB")}getConfig(){let t=(0,l.$t)({...c,...(0,o.zj)().packet});return t.showBits&&(t.paddingY+=10),t}getPacket(){return this.packet}pushWord(t){t.length>0&&this.packet.push(t)}clear(){(0,o.IU)(),this.packet=[]}},h=(0,n.K)((t,e)=>{(0,r.S)(t,e);let a=-1,i=[],l=1,{bitsPerRow:o}=e.getConfig();for(let{start:r,end:n,bits:d,label:c}of t.blocks){if(void 0!==r&&void 0!==n&&n<r)throw Error(`Packet block ${r} - ${n} is invalid. End must be greater than start.`);if((r??=a+1)!==a+1)throw Error(`Packet block ${r} - ${n??r} is not contiguous. It should start from ${a+1}.`);if(0===d)throw Error(`Packet block ${r} is invalid. Cannot have a zero bit field.`);for(n??=r+(d??1)-1,d??=n-r+1,a=n,s.R.debug(`Packet block ${r} - ${a} with label ${c}`);i.length<=o+1&&e.getPacket().length<1e4;){let[t,a]=p({start:r,end:n,bits:d,label:c},l,o);if(i.push(t),t.end+1===l*o&&(e.pushWord(i),i=[],l++),!a)break;({start:r,end:n,bits:d,label:c}=a)}}e.pushWord(i)},"populate"),p=(0,n.K)((t,e,a)=>{if(void 0===t.start)throw Error("start should have been set during first phase");if(void 0===t.end)throw Error("end should have been set during first phase");if(t.start>t.end)throw Error(`Block start ${t.start} is greater than block end ${t.end}.`);if(t.end+1<=e*a)return[t,void 0];let r=e*a-1,i=e*a;return[{start:t.start,end:r,label:t.label,bits:r-t.start},{start:i,end:t.end,label:t.label,bits:t.end-i}]},"getNextFittingBlock"),b={parser:{yy:void 0},parse:(0,n.K)(async t=>{let e=await (0,d.qg)("packet",t),a=b.parser?.yy;if(!(a instanceof k))throw Error("parser.parser?.yy was not a PacketDB. This is due to a bug within Mermaid, please report this issue at https://github.com/mermaid-js/mermaid/issues.");s.R.debug(e),h(e,a)},"parse")},u=(0,n.K)((t,e,a,r)=>{let l=r.db,s=l.getConfig(),{rowHeight:n,paddingY:d,bitWidth:c,bitsPerRow:k}=s,h=l.getPacket(),p=l.getDiagramTitle(),b=n+d,u=b*(h.length+1)-(p?0:n),g=c*k+2,$=(0,i.D)(e);for(let[t,e]of($.attr("viewBox",`0 0 ${g} ${u}`),(0,o.a$)($,u,g,s.useMaxWidth),h.entries()))f($,e,t,s);$.append("text").text(p).attr("x",g/2).attr("y",u-b/2).attr("dominant-baseline","middle").attr("text-anchor","middle").attr("class","packetTitle")},"draw"),f=(0,n.K)((t,e,a,{rowHeight:r,paddingX:i,paddingY:l,bitWidth:o,bitsPerRow:s,showBits:n})=>{let d=t.append("g"),c=a*(r+l)+l;for(let t of e){let e=t.start%s*o+1,a=(t.end-t.start+1)*o-i;if(d.append("rect").attr("x",e).attr("y",c).attr("width",a).attr("height",r).attr("class","packetBlock"),d.append("text").attr("x",e+a/2).attr("y",c+r/2).attr("class","packetLabel").attr("dominant-baseline","middle").attr("text-anchor","middle").text(t.label),!n)continue;let l=t.end===t.start,k=c-2;d.append("text").attr("x",e+(l?a/2:0)).attr("y",k).attr("class","packetByte start").attr("dominant-baseline","auto").attr("text-anchor",l?"middle":"start").text(t.start),l||d.append("text").attr("x",e+a).attr("y",k).attr("class","packetByte end").attr("dominant-baseline","auto").attr("text-anchor","end").text(t.end)}},"drawWord"),g={byteFontSize:"10px",startByteColor:"black",endByteColor:"black",labelColor:"black",labelFontSize:"12px",titleColor:"black",titleFontSize:"14px",blockStrokeColor:"black",blockStrokeWidth:"1",blockFillColor:"#efefef"},$={parser:b,get db(){return new k},renderer:{draw:u},styles:(0,n.K)(({packet:t}={})=>{let e=(0,l.$t)(g,t);return`
	.packetByte {
		font-size: ${e.byteFontSize};
	}
	.packetByte.start {
		fill: ${e.startByteColor};
	}
	.packetByte.end {
		fill: ${e.endByteColor};
	}
	.packetLabel {
		fill: ${e.labelColor};
		font-size: ${e.labelFontSize};
	}
	.packetTitle {
		fill: ${e.titleColor};
		font-size: ${e.titleFontSize};
	}
	.packetBlock {
		stroke: ${e.blockStrokeColor};
		stroke-width: ${e.blockStrokeWidth};
		fill: ${e.blockFillColor};
	}
	`},"styles")}}}]);