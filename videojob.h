#ifndef VIDEOJOB_H
#define VIDEOJOB_H

#include <QString>

struct VideoInfo {
    double duration;
    double fps;
    int width;
    int height;
    int audioBitrateKbps;
    int videoBitrateKbps;

};

struct VideoJob {
    QString inputPath;
    QString outputPath;
    VideoInfo videoInfo;
};
#endif // VIDEOJOB_H
