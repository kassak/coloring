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

QString smooth = "1";
QString edge_threshold = "15";
QString n_colors = "32";
QString resize;
int pal_idx = 1;
QMap<QRgb, int> palette;
std::vector<bool> visited;

QString create_segmented(QString file)
{
  QString res = file + ".seg.png";
  QStringList params;
  params << file;
  if (!resize.isNull()) params << "-resize" << resize;
  QProcess::execute("gmic", params
    << "-gimp_segment_watershed" << (edge_threshold + "," + smooth + ",0")
    << "-autoindex" << (n_colors + ",0,0")
    << "-o" << res
  );
  return res;
}

QString create_isophotes(QString file)
{
  QString res = file + ".iso.png";
  QProcess::execute("gmic", QStringList() << file
                    //<< "-isophotes" << QString("%1").arg(n_colors.toInt()*2)
                    //<< "-threshold" << "1"
                    //<< "-negative"
                    << "-edges" << "1"
                    << "-replace" << "1,255"
    << "-o" << res
  );
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

std::vector<int> find_component(QImage img, int start)
{
  std::vector<int> res, queue(1, start);
  QRgb bc = color(img, start);
  visited[start] = true;
  while (!queue.empty())
  {
    int pos = queue.back();
    queue.pop_back();
    res.push_back(pos);
    for (size_t i = 0; i < 4; ++i)
    {
      int n = neighbour(img, pos, i < 2 ? i*2 - 1 : 0, i >= 2 ? (i - 2)*2 - 1 : 0);
      if (n == -1 || visited[n] || color(img, n) != bc) continue;
      visited[n] = true;
      queue.push_back(n);
    }
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
  painter.drawText(x, y, QString("%1").arg(m));
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
    std::vector<int> comp = find_component(simg, start);
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

int main(int argc, char ** argv)
{
  QApplication app(argc, argv);
  int op = -1;
  while (-1 != (op = getopt(argc, argv, "s:e:r:c:")))
  {
     switch(op)
     {
     case 's':
       smooth = optarg;
       break;
     case 'e':
       edge_threshold = optarg;
       break;
     case 'c':
       n_colors = optarg;
       break;
     case 'r':
       resize = optarg;
       break;
     }
  }

  QString segmented = create_segmented(argv[optind]);
  QString isophotes = create_isophotes(segmented);
  QString distances = create_distance(isophotes);
  QString labeled = create_labels(segmented, distances);
  QString result = create_result(isophotes, labeled);
  return 0;
}
