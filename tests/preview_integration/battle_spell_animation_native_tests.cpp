#include "../../cczEditor2/PreviewIntegration/BattleSpellAnimation.h"
#include <iostream>
#include <cassert>
std::vector<unsigned char> read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int wmain(int argc,wchar_t** argv){
 if(argc!=4)return 2;
 Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;assert(Gdiplus::GdiplusStartup(&token,&input,nullptr)==Gdiplus::Ok);
 {
 auto archive=read(argv[1]);auto reference=read(argv[2]);int same=0;
 for(int i=0;i<60;++i){auto at=0x110+i*12;auto stored=ccz::editor_preview::battle_spell_detail::be32(archive,at),size=ccz::editor_preview::battle_spell_detail::be32(archive,at+4),offset=ccz::editor_preview::battle_spell_detail::be32(archive,at+8);
  if(stored!=size){std::vector<unsigned char> decoded;assert(ccz::editor_preview::battle_spell_detail::DecodeLsResource(archive,offset,stored,size,decoded));
   auto ro=ccz::editor_preview::battle_spell_detail::be32(reference,at+8);auto rs=ccz::editor_preview::battle_spell_detail::be32(reference,at);
   assert(decoded.size()==rs&&std::equal(decoded.begin(),decoded.end(),reference.begin()+ro));++same;
   std::vector<unsigned char> bad;assert(!ccz::editor_preview::battle_spell_detail::DecodeLsResource(archive,offset,1,size,bad));assert(bad.empty());
  }
  auto asset=ccz::editor_preview::LoadBattleSpellAnimation(argv[3],i);assert(!asset.frames.empty());assert(asset.width>0&&asset.height>0);assert(asset.frame_offsets.size()==asset.frames.size());
  if(i==0){assert(asset.frames.size()==21);assert(asset.frames[4]==asset.frames[6]);
   UINT count=0,bytes=0;Gdiplus::GetImageEncodersSize(&count,&bytes);std::vector<unsigned char> buf(bytes);auto enc=reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());Gdiplus::GetImageEncoders(count,bytes,enc);
   for(UINT j=0;j<count;++j)if(wcscmp(enc[j].MimeType,L"image/png")==0)assert(asset.frames[14]->Save(L".tmp-meff-native/fire.png",&enc[j].Clsid,nullptr)==Gdiplus::Ok);
  }
 }
 for(int i=100;i<=109;++i){auto asset=ccz::editor_preview::LoadBattleSpellAnimation(argv[3],i);assert(!asset.frames.empty());}
 std::vector<unsigned char> bad;assert(!ccz::editor_preview::battle_spell_detail::DecodeLsResource(archive,0,1,0,bad));
 std::cout<<"PASS: "<<same<<" compressed entries exactly match reference; all60 MEff and10 Mcall decoded; truncation rejected; repeated frames shared\n";
 }
 Gdiplus::GdiplusShutdown(token);
}
