#pragma once

#include <QVector>
#include <QtGlobal>

// Turns yt-dlp's per-file progress into one bar that only moves forward.
// yt-dlp restarts at 0% for each file (picture, then sound, then the next playlist video)
// and keeps revising its size guess for fragmented videos; both make a naive bar jump back.
class ProgressTracker
{
public:
    void reset();
    void startItem(int index, int count);   // "Downloading item 3 of 10" (playlists)
    void setPartCount(int parts);           // "Downloading 1 format(s): 137+140" = 2 parts
    void setExpected(const QList<qint64> &sizes) { expected_ = sizes; }   // until yt-dlp gives real sizes
    void startPart();                       // a new media file begins
    void finishPart();                      // "has already been downloaded"

    // Returns the overall fraction 0..1, or -1 while nothing is known yet.
    double update(qint64 done, qint64 total, int fragment, int fragments);
    double fraction() const { return shown_; }
    qint64 knownBytes() const;              // size of the current item's files seen so far

private:
    double itemFraction() const;
    double partWeight(int part) const;

    int item_ = 0, items_ = 1;
    int part_ = -1, parts_ = 1;
    QVector<qint64> sizes_;        // known size of each part of the current item
    QVector<double> partDone_;     // 0..1 per part of the current item
    QList<qint64> expected_;       // sizes known in advance (the preview), when they match the parts
    double shown_ = -1;
};
