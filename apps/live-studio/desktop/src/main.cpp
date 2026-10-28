#include "window.hpp"
#include "platform.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QImage>
#include <QTimer>
#include <iostream>

int main(int argc,char** argv) {
  // Select the offscreen platform before Qt initializes any native window.
  bool hidden=false;
  for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--hidden")hidden=true;
  if(hidden)studio_app::configureOffscreenPlatform();
  QApplication app(argc,argv);
  QApplication::setApplicationName("AV Live Studio");
  QCommandLineParser parser;parser.setApplicationDescription(QStringLiteral("Qt 6 直播助手"));parser.addHelpOption();
  parser.addOption({"hidden",QStringLiteral("离屏运行，不访问设备或保存的账号")});
  parser.addOption({"frames",QStringLiteral("渲染 N 次后退出"),"N","0"});
  parser.addOption({"settings",QStringLiteral("项目配置目录"),"directory"});
  parser.addOption({"screenshot",QStringLiteral("保存程序内渲染截图"),"file"});
  parser.addOption({"portrait",QStringLiteral("使用 720 × 1280 画布")});
  parser.addOption({"test-density",QStringLiteral("离屏截图像素倍率，1–2"),"scale","1"});
  parser.addOption({"ui-scale",QStringLiteral("界面字号倍率，1–2；屏幕 DPI 由 Qt 自动处理"),"scale","1"});
  parser.process(app);
  try {
    bool valid=false;int frames=parser.value("frames").toInt(&valid);if(!valid||frames<0)throw std::invalid_argument("--frames must be a nonnegative integer");
    double density=parser.value("test-density").toDouble(&valid);if(!valid||density<1||density>2||(!hidden&&density!=1))throw std::invalid_argument("--test-density must be 1..2 and requires --hidden");
    double scale=parser.value("ui-scale").toDouble(&valid);if(!valid||scale<1||scale>2)throw std::invalid_argument("--ui-scale must be 1..2");
    if(hidden&&!parser.isSet("settings"))throw std::invalid_argument("--hidden requires an isolated --settings directory");
    studio_app::applyTheme();
    if(scale!=1)app.setStyleSheet(app.styleSheet()+QString("\nQWidget { font-size: %1px; }").arg(13*scale));
    studio_app::WindowOptions config;
    config.offline=hidden;config.portrait=parser.isSet("portrait");config.settings=parser.isSet("settings")?std::filesystem::u8path(parser.value("settings").toUtf8().toStdString()):studio_app::platform::default_settings();
    studio_app::Window window(config);
    if(hidden)window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show();
    int count=0,result=0;bool captured=false;QString screenshot=parser.value("screenshot");
    QTimer timer;
    QObject::connect(&timer,&QTimer::timeout,&app,[&]{
      ++count;
      auto media=window.mediaSnapshot();
      bool finish=frames>0&&count>=frames;
      if(!screenshot.isEmpty()&&!captured&&count>=10&&media.preview){
        QImage image(QSize(qRound(window.width()*density),qRound(window.height()*density)),QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(density);image.fill(Qt::transparent);window.render(&image);
        if(!image.save(screenshot)){std::cerr<<"Cannot save screenshot\n";result=1;finish=true;}else captured=true;
      }
      if(finish){
        if(!screenshot.isEmpty()&&!captured){std::cerr<<"No screenshot produced\n";result=1;}
        if(hidden&&!media.error.empty()){std::cerr<<media.error<<'\n';result=1;}
        if(!window.saveProject())result=1;
        app.exit(result);
      }
    });
    timer.start(33);
    return app.exec();
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
