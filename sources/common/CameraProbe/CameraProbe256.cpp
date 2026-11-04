#define NOMINMAX
#include <windows.h>
#include <MemoryBuffer.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.MediaProperties.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Media.Capture.h>
#include <winrt/Windows.Media.Capture.Frames.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Devices::Enumeration;
using namespace winrt::Windows::Media::Capture;
using namespace winrt::Windows::Media::Capture::Frames;
using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Storage::Streams;
static void Mark(char const* s){LARGE_INTEGER q;QueryPerformanceCounter(&q);printf("STEP256 %s qpc=%lld\n",s,q.QuadPart);}
static void Save(std::wstring const& dir,SoftwareBitmap const& bitmap){
 bool rgb=bitmap.BitmapPixelFormat()==BitmapPixelFormat::Bgra8;Buffer buffer(uint32_t(bitmap.PixelWidth()*bitmap.PixelHeight()*(rgb?4:1)* (rgb?2:3)/2));bitmap.CopyToBuffer(buffer);auto data=DataReader::FromBuffer(buffer);std::vector<uint8_t> bytes(buffer.Length());data.ReadBytes(bytes);data.Close();
 std::wstring p=dir+(rgb?L"\\preview.bgra":L"\\preview.nv12");if(GetFileAttributesW(p.c_str())!=INVALID_FILE_ATTRIBUTES)throw hresult_error(E_UNEXPECTED,L"Preserve existing image");
 std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<char const*>(bytes.data()),bytes.size());f.close();if(!f)throw hresult_error(E_FAIL,L"Image save failed");
 std::ofstream meta(dir+L"\\layout.json");meta<<"{\"width\":"<<bitmap.PixelWidth()<<",\"height\":"<<bitmap.PixelHeight()<<",\"format\":\""<<(rgb?"BGRA8":"NV12")<<"\",\"bytes\":"<<bytes.size()<<"}";meta.close();if(!meta)throw hresult_error(E_FAIL,L"Layout save failed");
}
int wmain(int argc,wchar_t**argv){
 setvbuf(stdout,nullptr,_IONBF,0);
 if(argc==2&&(std::wstring(argv[1])==L"--help"||std::wstring(argv[1])==L"--config-only")){puts("CONFIG256 cameraOpened=false usage=CameraProbe.exe --name NAME --seconds 4 [--save-dir DIR]");return 0;}
 std::wstring name,dir;unsigned seconds=4;
 for(int i=1;i<argc;++i){std::wstring arg=argv[i];if((arg==L"--name"||arg==L"--seconds"||arg==L"--save-dir")&&i+1<argc){auto v=argv[++i];if(arg==L"--name")name=v;else if(arg==L"--save-dir")dir=v;else seconds=wcstoul(v,nullptr,10);}else return 2;}
 if(name.empty()||seconds<3||seconds>15||(!dir.empty()&&GetFileAttributesW(dir.c_str())==INVALID_FILE_ATTRIBUTES))return 2;
 init_apartment(apartment_type::multi_threaded);MediaCapture capture{nullptr};MediaFrameReader reader{nullptr};bool started=false,saved=false;hresult result=S_OK;unsigned frames=0,duplicates=0;long long last=-1;double sum=0;unsigned minY=255,maxY=0;uint64_t sampled=0;uint32_t fnv=2166136261u;int width=0,height=0;
 try{
  LARGE_INTEGER freq;QueryPerformanceFrequency(&freq);printf("CLOCK256 frequency=%lld\n",freq.QuadPart);auto devices=DeviceInformation::FindAllAsync(DeviceClass::VideoCapture).get();hstring id;unsigned matches=0;
  for(auto const& d:devices)if(d.Name()==name){id=d.Id();++matches;}if(matches!=1)throw hresult_error(E_UNEXPECTED,L"Exactly one named camera required");
  MediaCaptureInitializationSettings settings;settings.VideoDeviceId(id);settings.StreamingCaptureMode(StreamingCaptureMode::Video);settings.MemoryPreference(MediaCaptureMemoryPreference::Cpu);settings.SharingMode(MediaCaptureSharingMode::ExclusiveControl);
  capture=MediaCapture();Mark("initialize_begin");capture.InitializeAsync(settings).get();Mark("initialize_complete");
  MediaFrameSource source{nullptr};MediaFrameFormat chosen{nullptr};int best=-1;
  for(auto const& kv:capture.FrameSources()){auto s=kv.Value();if(s.Info().SourceKind()!=MediaFrameSourceKind::Color)continue;for(auto const& f:s.SupportedFormats()){
   auto v=f.VideoFormat();wprintf(L"SOURCE256 subtype=%s width=%u height=%u stream=%u\n",f.Subtype().c_str(),v.Width(),v.Height(),unsigned(s.Info().MediaStreamType()));if(v.Width()>1920||v.Height()>1080||v.Width()<320||v.Height()<240)continue;
   int score=int(v.Width()*v.Height())+(f.Subtype()==L"NV12"?6000000:0)+(s.Info().MediaStreamType()==MediaStreamType::VideoPreview?3000000:0);if(score>best){best=score;source=s;chosen=f;}
  }}if(!source||!chosen)throw hresult_error(E_NOTIMPL,L"Color frame source unavailable");
  source.SetFormatAsync(chosen).get();width=int(chosen.VideoFormat().Width());height=int(chosen.VideoFormat().Height());wprintf(L"FORMAT256 width=%d height=%d subtype=%s stream=%u\n",width,height,chosen.Subtype().c_str(),unsigned(source.Info().MediaStreamType()));
  reader=capture.CreateFrameReaderAsync(source,chosen.Subtype()==L"NV12"?L"NV12":L"BGRA8").get();reader.AcquisitionMode(MediaFrameReaderAcquisitionMode::Realtime);Mark("reader_start_begin");if(reader.StartAsync().get()!=MediaFrameReaderStartStatus::Success)throw hresult_error(E_FAIL,L"Frame reader did not start");started=true;Mark("reader_started");
  ULONGLONG begin=GetTickCount64();while(GetTickCount64()-begin<seconds*1000ull){
   auto frame=reader.TryAcquireLatestFrame();if(!frame){Sleep(5);continue;}
   try{auto stamp=frame.SystemRelativeTime();if(stamp&&stamp.Value().count()==last){++duplicates;frame.Close();Sleep(5);continue;}if(stamp)last=stamp.Value().count();auto media=frame.VideoMediaFrame();auto bitmap=media?media.SoftwareBitmap():SoftwareBitmap{nullptr};
    if(bitmap){bool rgb=bitmap.BitmapPixelFormat()==BitmapPixelFormat::Bgra8;if((!rgb&&bitmap.BitmapPixelFormat()!=BitmapPixelFormat::Nv12)||bitmap.PixelWidth()!=width||bitmap.PixelHeight()!=height)throw hresult_error(E_UNEXPECTED,L"Unexpected NV12 shape");
     auto lock=bitmap.LockBuffer(BitmapBufferAccessMode::Read);auto ref=lock.CreateReference();auto access=ref.as<::Windows::Foundation::IMemoryBufferByteAccess>();BYTE* bytes=nullptr;UINT32 extent=0;check_hresult(access->GetBuffer(&bytes,&extent));auto p=lock.GetPlaneDescription(0);
     if(p.StartIndex<0||p.Stride<width*(rgb?4:1)||uint64_t(p.StartIndex)+uint64_t(height-1)*p.Stride+width*(rgb?4:1)>extent)throw hresult_error(E_UNEXPECTED,L"Plane extent rejected");
     for(int y=0;y<height;y+=32)for(int x=0;x<width;x+=32){auto pixel=bytes+p.StartIndex+size_t(y)*p.Stride+x*(rgb?4:1);unsigned v=rgb?(29*pixel[0]+150*pixel[1]+77*pixel[2]+128)/256:pixel[0];sum+=v;++sampled;if(v<minY)minY=v;if(v>maxY)maxY=v;fnv=(fnv^v)*16777619u;}
     access=nullptr;ref.Close();lock.Close();++frames;if(!dir.empty()&&!saved&&GetTickCount64()-begin>=2500){Save(dir,bitmap);saved=true;}bitmap.Close();
    }frame.Close();
   }catch(...){frame.Close();throw;}Sleep(5);
  }
 }catch(hresult_error const& e){result=e.code();wprintf(L"ERROR256 hr=%08x message=%s\n",unsigned(result),e.message().c_str());}catch(...){result=E_UNEXPECTED;puts("ERROR256 unexpected");}
 auto close=[&](char const* label,auto f){try{f();printf("CLOSE256 %s hr=00000000\n",label);}catch(hresult_error const&e){result=e.code();printf("CLOSE256 %s hr=%08x\n",label,unsigned(e.code()));}};
 if(reader&&started)close("reader_stop",[&]{reader.StopAsync().get();started=false;});if(reader)close("reader_close",[&]{reader.Close();reader=nullptr;});if(capture)close("capture_close",[&]{capture.Close();capture=nullptr;});
 bool signal=sampled&&maxY>0;printf("RESULT256 hr=%08x frames=%u duplicatePolls=%u width=%d height=%d meanY=%.6f minY=%u maxY=%u hash=%08x signal=%s saved=%s controlsChanged=false\n",unsigned(result),frames,duplicates,width,height,sampled?sum/sampled:0.,minY,maxY,fnv,signal?"true":"false",saved?"true":"false");uninit_apartment();return result.value>=0&&frames>=15&&signal&&(dir.empty()||saved)?0:5;
}
