#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>
#include "drv_touch.h"
#include "setting.h"
#include "video_pipeline.h"
#include "../../face_studio/src/ios_overlay.h"
#include "../../face_studio/src/mode_persistence.h"
#include "../../face_studio/src/startup_spinner.h"
#include "../../face_studio/src/ui_localization.h"

enum class Mode { Barcode, QRCode, AprilTag, AprilPose, Count };
static std::atomic<bool> stop(false), menu(false);
static std::atomic<Mode> mode(Mode::Barcode);
static constexpr float kTagSizeMm = 50.f, kFocalPx = 520.f;

static const char *localized_mode_name(Mode selected) {
    switch (selected) {
    case Mode::Barcode:
        return dshanpi_ui::localized("Barcode", "条形码", "條碼",
                                     "バーコード");
    case Mode::QRCode:
        return dshanpi_ui::localized("QR Code", "二维码", "二維碼",
                                     "QRコード");
    case Mode::AprilTag:
        return "AprilTag";
    case Mode::AprilPose:
        return dshanpi_ui::localized("AprilTag Pose", "AprilTag 姿态",
                                     "AprilTag 姿態", "AprilTag 姿勢");
    default: return "";
    }
}

static void touch_proc() {
    drv_touch_inst_t *t=nullptr; if (drv_touch_inst_create(0,&t)!=0) return;
    auto last=std::chrono::steady_clock::now()-std::chrono::milliseconds(500);
    while(!stop){ drv_touch_data p[DRV_TOUCH_POINT_NUMBER_MAX]; int n=drv_touch_read(t,p,DRV_TOUCH_POINT_NUMBER_MAX);
        if(n<=0){usleep(10000);continue;} auto now=std::chrono::steady_clock::now();
        if(p[0].event!=DRV_TOUCH_EVENT_DOWN||now-last<std::chrono::milliseconds(250))continue; last=now;
        int x=639-(int)p[0].y_coordinate,y=479-(int)p[0].x_coordinate;
        if (y < ios_ui::kBackTouchExtent &&
            x < ios_ui::kBackTouchExtent) stop=true;
        else if(y<78&&x>500)menu=!menu.load();
        else if(menu&&x>=350&&y>=78&&y<318){int i=(y-78)/60;if(i<4&&i!=(int)mode.load()){mode=(Mode)i;dshanpi_mode_state::save("code_scanner",i,(int)Mode::Count);}menu=false;}
    } drv_touch_inst_destroy(&t);
}

static std::vector<cv::Point2f> order_quad(const std::vector<cv::Point>& p) {
    std::vector<cv::Point2f> q(4); float mn=1e9,mx=-1e9,md=1e9,xd=-1e9;
    for(auto &v:p){float s=v.x+v.y,d=v.x-v.y;if(s<mn){mn=s;q[0]=v;}if(d>xd){xd=d;q[1]=v;}
        if(s>mx){mx=s;q[2]=v;}if(d<md){md=d;q[3]=v;}} return q;
}
static std::vector<std::vector<cv::Point2f>> square_candidates(const cv::Mat &gray) {
    cv::Mat b; cv::adaptiveThreshold(gray,b,255,cv::ADAPTIVE_THRESH_GAUSSIAN_C,cv::THRESH_BINARY_INV,31,7);
    std::vector<std::vector<cv::Point>> cs; cv::findContours(b,cs,cv::RETR_LIST,cv::CHAIN_APPROX_SIMPLE);
    std::vector<std::vector<cv::Point2f>> out;
    for(auto &c:cs){std::vector<cv::Point> p;cv::approxPolyDP(c,p,.04*cv::arcLength(c,true),true);
        double a=std::fabs(cv::contourArea(p));if(p.size()==4&&cv::isContourConvex(p)&&a>1200&&a<gray.total()*.8)out.push_back(order_quad(p));}
    std::sort(out.begin(),out.end(),[](auto&a,auto&b){return std::fabs(cv::contourArea(a))>std::fabs(cv::contourArea(b));});
    if(out.size()>6)out.resize(6);return out;
}
static int read_tag(const cv::Mat &gray,const std::vector<cv::Point2f>&q) {
    std::vector<cv::Point2f>d={{0,0},{79,0},{79,79},{0,79}};cv::Mat w;
    cv::warpPerspective(gray,w,cv::getPerspectiveTransform(q,d),cv::Size(80,80));cv::threshold(w,w,0,255,cv::THRESH_BINARY|cv::THRESH_OTSU);
    int id=0;for(int y=2;y<6;y++)for(int x=2;x<6;x++){cv::Rect r(x*10+2,y*10+2,6,6);id=(id<<1)|(cv::mean(w(r))[0]>127);}return id;
}
static void draw_quad(cv::Mat&o,const std::vector<cv::Point2f>&q,const cv::Scalar&c){for(int i=0;i<4;i++)cv::line(o,q[i],q[(i+1)%4],c,3);}
static void tag_pose(cv::Mat&o,const std::vector<cv::Point2f>&q) {
    float h=kTagSizeMm/2;std::vector<cv::Point3f>obj={{-h,-h,0},{h,-h,0},{h,h,0},{-h,h,0}};
    cv::Mat k=(cv::Mat_<double>(3,3)<<kFocalPx,0,320,0,kFocalPx,240,0,0,1),dist=cv::Mat::zeros(1,5,CV_64F),r,t;
    if(!cv::solvePnP(obj,q,k,dist,r,t))return;std::vector<cv::Point3f>axis={{0,0,0},{30,0,0},{0,30,0},{0,0,-30}};std::vector<cv::Point2f>p;
    cv::projectPoints(axis,r,t,k,dist,p);cv::line(o,p[0],p[1],{0,0,255,255},3);cv::line(o,p[0],p[2],{0,255,0,255},3);cv::line(o,p[0],p[3],{255,0,0,255},3);
    char s[80];snprintf(s,sizeof(s),"XYZ %.0f %.0f %.0f mm",t.at<double>(0),t.at<double>(1),t.at<double>(2));cv::putText(o,s,q[0],cv::FONT_HERSHEY_SIMPLEX,.5,{0,255,255,255},2);
}
static cv::Rect barcode_region(const cv::Mat&g){cv::Mat x,b;cv::Sobel(g,x,CV_32F,1,0,3);cv::convertScaleAbs(x,x);cv::threshold(x,b,0,255,cv::THRESH_BINARY|cv::THRESH_OTSU);cv::morphologyEx(b,b,cv::MORPH_CLOSE,cv::getStructuringElement(cv::MORPH_RECT,{25,5}));
    std::vector<std::vector<cv::Point>>c;cv::findContours(b,c,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);cv::Rect best;for(auto&v:c){auto r=cv::boundingRect(v);if(r.area()>best.area()&&r.width>r.height*1.5)best=r;}return best;}
static std::string ean13(const cv::Mat&g,const cv::Rect&r){if(r.area()==0)return{};cv::Mat line;cv::resize(g(r),line,{190,40});cv::threshold(line,line,0,255,cv::THRESH_BINARY|cv::THRESH_OTSU);std::string bits;for(int i=0;i<95;i++)bits+=(cv::mean(line(cv::Rect(i*2,10,2,20)))[0]<128?'1':'0');
    static const char*L[]={"0001101","0011001","0010011","0111101","0100011","0110001","0101111","0111011","0110111","0001011"};static const char*R[]={"1110010","1100110","1101100","1000010","1011100","1001110","1010000","1000100","1001000","1110100"};
    if(bits.substr(0,3)!="101"||bits.substr(45,5)!="01010")return{};std::string out="?";for(int n=0;n<6;n++){std::string s=bits.substr(3+n*7,7);int d=-1;for(int j=0;j<10;j++)if(s==L[j])d=j;if(d<0)return{};out+=char('0'+d);}for(int n=0;n<6;n++){std::string s=bits.substr(50+n*7,7);int d=-1;for(int j=0;j<10;j++)if(s==R[j])d=j;if(d<0)return{};out+=char('0'+d);}return out;}

static void process(const cv::Mat&rgb,cv::Mat&o){o.setTo(cv::Scalar(0,0,0,0));cv::Mat g;cv::cvtColor(rgb,g,cv::COLOR_RGB2GRAY);Mode m=mode;
    if(m==Mode::QRCode){cv::QRCodeDetector q;std::vector<cv::Point>p;std::string s=q.detectAndDecode(g,p);if(p.size()==4){std::vector<cv::Point2f>f(p.begin(),p.end());draw_quad(o,f,{0,255,0,255});}if(!s.empty())cv::putText(o,s,{20,440},cv::FONT_HERSHEY_SIMPLEX,.65,{0,255,0,255},2);}
    else if(m==Mode::Barcode){cv::Rect r=barcode_region(g);if(r.area()){ios_ui::rounded_rect(o,r,{0,255,0,255},3);std::string s=ean13(g,r);cv::putText(o,s.empty()?"Barcode detected":s,{r.x,std::max(30,r.y-8)},cv::FONT_HERSHEY_SIMPLEX,.6,{0,255,0,255},2);}}
    else {auto qs=square_candidates(g);for(auto&q:qs){draw_quad(o,q,{0,255,0,255});char s[32];snprintf(s,sizeof(s),"Tag %d",read_tag(g,q));cv::putText(o,s,q[0],cv::FONT_HERSHEY_SIMPLEX,.55,{0,255,255,255},2);if(m==Mode::AprilPose)tag_pose(o,q);}}
}
static void controls(cv::Mat&o){ios_ui::rounded_rect(o,{12,12,52,52},{40,40,40,230},-1);cv::putText(o,"<",{29,46},cv::FONT_HERSHEY_SIMPLEX,.62,{255,255,255,255},2);dshanpi_ui::draw_mode_header(o,localized_mode_name(mode.load()),52);ios_ui::rounded_rect(o,{500,12,128,52},{40,40,40,230},-1);dshanpi_ui::draw_text_centered(o,dshanpi_ui::common_text(dshanpi_ui::CommonText::Mode),{500,12,128,52},20,{255,255,255,255});
    if(menu){ios_ui::rounded_rect(o,{346,72,282,250},{28,32,40,245},-1);for(int i=0;i<4;i++){int y=78+i*60;if(i==(int)mode.load())ios_ui::rounded_rect(o,{352,y,270,52},ios_ui::accent(),-1);dshanpi_ui::draw_text(o,localized_mode_name(static_cast<Mode>(i)),{370,y+36},20,{255,255,255,255});}}}
int main(int argc,char**argv){int csi=argc>1?atoi(argv[1]):2;if(csi!=0&&csi!=2)csi=2;mode=(Mode)dshanpi_mode_state::load("code_scanner",(int)Mode::Barcode,(int)Mode::Count);CameraManager cam(0,csi);if(cam.Create()!=0)return 1;std::thread tt(touch_proc);cv::Mat o(480,640,CV_8UC4);int startup_phase=0;dshanpi_ui::draw_startup_spinner(o,startup_phase);cam.InsertFrame(o.data);size_t pl=640*480;std::vector<uint8_t>b(pl*3);cv::Mat ch[]={cv::Mat(480,640,CV_8UC1,b.data()),cv::Mat(480,640,CV_8UC1,b.data()+pl),cv::Mat(480,640,CV_8UC1,b.data()+pl*2)},rgb;
    while(!stop){DumpRes f;if(cam.GetFrame(f)!=0){startup_phase=(startup_phase+18)%360;dshanpi_ui::draw_startup_spinner(o,startup_phase);cam.InsertFrame(o.data);usleep(40000);continue;}auto*s=(uint8_t*)f.virt_addr;for(int c=0;c<3;c++)std::reverse_copy(s+c*pl,s+(c+1)*pl,b.data()+c*pl);cv::merge(ch,3,rgb);process(rgb,o);controls(o);cam.InsertFrame(o.data);cam.ReleaseFrame(f);}tt.join();cam.Destroy();usleep(200000);return 0;}
