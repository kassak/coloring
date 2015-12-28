#include <QtGui/QImage>
#include <QtGui/QApplication>
#include <QtGui/QPainter>
#include <QtCore/QCoreApplication>
#include <QtCore/QProcess>
#include <QtCore/QStringList>
#include <QtCore/QMap>
#include <unistd.h>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <vector>
#include <set>

QString edge_fid = "1";
QString edge_simpl = "3";
QString edge_threshold = "15";
QString seg_smooth = "1.5";
QString n_colors = "32";
QString kuwahara_rad = "5";
int area_threshold = 0;
int importance_threshold = 0xFFFFFF;
int columns = 11;
QString downscale;
QString upscale;
int pal_idx = 1;
QMap<QRgb, int> palette;
std::vector<bool> visited;

QString create_segmented(QString file)
{
  QString res = file + ".seg.png";
  QStringList params;
  params << file;
  if (!downscale.isNull()) params << "-resize" << downscale + "%," + downscale + "%";
  params
    << "-kuwahara" << kuwahara_rad
    << "-gimp_segment_watershed" << (edge_threshold + "," + seg_smooth + ",1")
    << "-autoindex" << (n_colors + ",0,0")
      //<< "-gimp_cutout" << (n_colors + "," + edge_simpl + "," + edge_fid + ",0")
      ;
  if (!upscale.isNull()) params << "-resize" << upscale + "%," + upscale + "%";
  params << "-gimp_cutout" << (n_colors + "," + edge_simpl + "," + edge_fid + ",0");
  params
      << "-autoindex" << (n_colors + ",0,0")
    << "-o" << res;
  QProcess::execute("gmic", params);
  return res;
}

QString create_result(QString isophotes, QString labeled)
{
  QString res = labeled + ".res.png";
  QProcess::execute("gmic", QStringList() << isophotes << labeled
    << "-blend" << "multiply,1"
    << "-o" << res
  );
  return res;
}

QString create_distance(QString file)
{
  QString res = file + ".dist.png";
  QProcess::execute("gmic", QStringList() << file
    //<< "-frame" << "1,1,0,0,0"
    << "-gimp_distance" << "0,1,1,32"
    << "-o" << res
  );
  return res;
}

int find_start_idx(int start)
{
  for (size_t i = start; i < visited.size(); ++i)
    if (!visited[i])
      return i;
  return -1;
}

QRgb color(QImage img, int start)
{
  int w = img.width();
  return img.pixel(start % w, start / w);
}

int neighbour(QImage img, int p, int x, int y)
{
  int w = img.width();
  int h = img.height();
  if (p % w + x < 0 || p % w + x >= w) return -1;
  if (p / w + y < 0 || p / w + y >= h) return -1;
  return p + x + w*y;
}

std::vector<int> find_component(QImage img, int start, std::vector<int> * const brd, std::set<int> * const obrd)
{
  std::vector<int> res, queue(1, start);
  QRgb bc = color(img, start);
  visited[start] = true;
  while (!queue.empty())
  {
    int pos = queue.back();
    queue.pop_back();
    res.push_back(pos);
    bool on_brd = false;
    for (size_t i = 0; i < 4; ++i)
    {
      int n = neighbour(img, pos, i < 2 ? i*2 - 1 : 0, i >= 2 ? (i - 2)*2 - 1 : 0);
      if (n != -1 && color(img, n) == bc)
      {
        if (!visited[n])
        {
          visited[n] = true;
          queue.push_back(n);
        }
        continue;
      }
      if (obrd != NULL && n != -1) obrd->insert(n);
      on_brd = true;
    }
    if (on_brd && brd != NULL) brd->push_back(pos);
  }
  return res;
}

int center(QImage const & img, std::vector<int> const & comp)
{
  int w = img.width();
  int x0, x1, y0, y1;
  x0 = x1 = comp[0] % w;
  y0 = y1 = comp[0] / w;
  for (size_t i = 0; i < comp.size(); ++i)
  {
    int x = comp[i] % w;
    int y = comp[i] / w;
    x0 = std::min(x0, x);
    x1 = std::max(x1, x);
    y0 = std::min(y0, y);
    y1 = std::max(y1, y);
  }
  return (x0 + x1)/2 + (y0 + y1)/2*w;
}

int value(QRgb const & c)
{
  return c & 0xFF;
}

int dist(QImage const & img, int p1, int p2)
{
  int w = img.width();
  return std::abs(p1 % w - p2 % 2) + std::abs(p1 / w - p2 / w);
}

int find_label_pos(QImage const & img, std::vector<int> const & comp)
{
  int c = center(img, comp);
  int p = comp[0];
  int v = value(color(img, p));
  int d = dist(img, p, c);
  for (size_t i = 0; i < comp.size(); ++i)
  {
    int cv = value(color(img, comp[i]));
    int cd = dist(img, c, comp[i]);
    if (cv < v || cv == v && cd > d) continue;
    v = cv;
    d = cd;
    p = comp[i];
  }
  return p;
}

void mark(QImage const & img, int p, QPainter & painter)
{
  int x = p % img.width();
  int y = p / img.width();
  QRgb c = color(img, p);
  QMap<QRgb, int>::const_iterator it = palette.find(c);
  int m = pal_idx;
  if (it == palette.end())
    palette[c] = pal_idx++;
  else
    m = *it;
  painter.drawText(x - 10, y - 5, 20, 10, Qt::AlignHCenter | Qt::AlignVCenter, QString("%1").arg(m));
  //painter.drawRect(x -5,y - 5, 10, 10);
}

QString create_labels(QString segmented, QString distances)
{
  QImage simg(segmented);
  QImage dimg(distances);
  QImage mimg(simg.size(), QImage::Format_RGB888);
  int w = dimg.width();
  int h = dimg.height();
  int w2 = simg.width();
  int h2 = simg.height();
  if (h2 != h || w2 != w)
  {
    std::stringstream ss;
    ss << "size mismatch " << h << "-" << h2 << " " << w << "-" << w2;
    throw std::runtime_error(ss.str());
  }
  visited.assign(w*h, false);
  QPainter painter;
  painter.begin(&mimg);
  painter.fillRect(0, 0, w, h, QColor::fromRgb(255, 255, 255));
  int start = 0;
  while (start != -1)
  {
    //std::cout << "extracting comp..." << std::endl;
    std::vector<int> comp = find_component(simg, start, NULL, NULL);
    //std::cout << comp.size() << " pixels" << std::endl;
    //std::cout << "searching label pos..." << std::endl;
    int p = find_label_pos(dimg, comp);
    //std::cout << "placing mark..." << std::endl;
    mark(simg, p, painter);
    //std::cout << "searching next comp..." << std::endl;
    start = find_start_idx(start);
  }
  painter.end();
  QString res = distances + ".num.png";
  mimg.save(res, "PNG");
  return res;
}

QString create_isophotes(QString file)
{
  QImage simg(file);
  QImage iimg(simg.size(), QImage::Format_RGB888);
  int w = simg.width();
  int h = simg.height();
  visited.assign(w*h, false);
  QPainter painter;
  painter.begin(&iimg);
  painter.fillRect(0, 0, w, h, QColor::fromRgb(255, 255, 255));
  int start = 0;
  while (start != -1)
  {
    std::vector<int> brd;
    find_component(simg, start, &brd, NULL);
    for (size_t i = 0; i < brd.size(); ++i)
      iimg.setPixel(brd[i]%w, brd[i]/w, 0);
    start = find_start_idx(start);
  }
  painter.end();
  QString res = file + ".iso.png";
  iimg.save(res, "PNG");
  return res;
}

void ycbcr(QRgb c, int & y, int &cb, int &cr)
{
  QColor cc(c);
  y = 0.299*cc.red() + 0.587*cc.green() + 0.114*cc.blue();
  cb = 128 - 0.168736*cc.red() - 0.331264*cc.green() + 0.5*cc.blue();
  cr = 128 + 0.5*cc.red() - 0.418688*cc.green() - 0.081312*cc.blue();
}

int distance(QRgb a, QRgb b)
{
  int ah, as, av, bh, bs, bv;
  ycbcr(a, ah, as, av);
  ycbcr(b, bh, bs, bv);
  return 2*std::abs(ah - bh) + std::abs(as - bs) + std::abs(av - bv);
}

QString remove_small_segments(QString file)
{
  QImage bimg(file);
  QImage simg = bimg.copy();
  int w = simg.width();
  int h = simg.height();
  visited.assign(w*h, false);
  int start = 0;
  while (start != -1)
  {
    std::set<int> obrd;
    std::vector<int> comp = find_component(bimg, start, NULL, &obrd);
    if (comp.size() < area_threshold)
    {
      QRgb sc = color(bimg, start);
      std::set<QRgb> colors;
      for (std::set<int>::const_iterator it = obrd.begin(); it != obrd.end(); ++it)
        colors.insert(color(bimg, *it));
      int d = importance_threshold;
      QRgb c = -1;
      for (std::set<QRgb>::const_iterator it = colors.begin(); it != colors.end(); ++it)
      {
        int dd = distance(sc, *it);
        if (dd < d)
        {
          d = dd;
          c = *it;
        }
      }
      if (c != -1)
        for (size_t i = 0; i < comp.size(); ++i)
          simg.setPixel(comp[i]%w, comp[i]/w, c);
    }
    start = find_start_idx(start);
  }
  QString res = file + ".del.png";
  simg.save(res, "PNG");
  return res;
}

QString create_palette(QString file)
{
  int n = palette.size();
  int rows = (n + columns - 1) / columns;
  int rect_size = 40;
  QImage img(columns*rect_size, rows*rect_size, QImage::Format_RGB888);
  QPainter painter;
  painter.begin(&img);
  painter.fillRect(0, 0, img.width(), img.height(), QColor(255, 255, 255));
  QMap<int, QRgb> rpalette;
  for(QMap<QRgb, int>::const_iterator it = palette.begin(); it != palette.end(); ++it)
      rpalette.insert(it.value(), it.key());
  for (int i = 0; i < n; ++i)
  {
    QColor c = QColor(rpalette[i+1]);
    int x = (i % columns)*rect_size;
    int y = (i / columns)*rect_size;
    painter.setPen(c.lightness() < 128 ? QColor(255, 255, 255) : QColor(0, 0, 0));
    painter.fillRect(x, y, rect_size, rect_size, c);
    painter.drawText(x + 0.1*rect_size, y + 0.25*rect_size, 0.8*rect_size, 0.5*rect_size,
                     Qt::AlignVCenter | Qt::AlignHCenter, QString("%1").arg(i+1));
  }
  painter.end();
  QString res = file + ".pal.png";
  img.save(res, "PNG");
  return res;
}

int main(int argc, char ** argv)
{
  QApplication app(argc, argv);
  int op = -1;
  while (-1 != (op = getopt(argc, argv, "s:f:c:a:i:k:t:m:d:u:l:")))
  {
     switch(op)
     {
     case 'a':
       area_threshold = QString(optarg).toInt();
       break;
     case 'i':
       importance_threshold = QString(optarg).toInt();
       break;
     case 't':
       edge_threshold = optarg;
       break;
     case 'm':
       seg_smooth = optarg;
       break;
     case 'l':
       columns = QString(optarg).toInt();
       break;
     case 'd':
       downscale = optarg;
       downscale = QString("%1").arg((int)(100/downscale.toDouble()));
       break;
     case 's':
       edge_simpl = optarg;
       break;
     case 'k':
       kuwahara_rad = optarg;
       break;
     case 'f':
       edge_fid = optarg;
       break;
     case 'c':
       n_colors = optarg;
       break;
     case 'u':
       upscale = optarg;
       upscale = QString("%1").arg((int)(100*upscale.toDouble()));
       break;
     }
  }

  QString bad_segmented = create_segmented(argv[optind]);
  QString segmented = remove_small_segments(bad_segmented);
  QString isophotes = create_isophotes(segmented);
  QString distances = create_distance(isophotes);
  QString labeled = create_labels(segmented, distances);
  QString result = create_result(isophotes, labeled);
  QString pal = create_palette(argv[optind]);
  return 0;
}
