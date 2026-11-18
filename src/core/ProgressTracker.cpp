#include "ProgressTracker.h"

#include <algorithm>

namespace {

// Before the sound part starts, its size is unknown; it is usually about a tenth of the picture.
constexpr double guessedLaterPartShare = 0.1;

}

void ProgressTracker::reset()
{
    const QList<qint64> expected = expected_;   // known before the download, so it survives a retry
    *this = ProgressTracker{};
    expected_ = expected;
}

void ProgressTracker::startItem(int index, int count)
{
    item_ = qMax(0, index - 1);
    items_ = qMax(1, count);
    part_ = -1;
    parts_ = 1;
    sizes_.clear();
    partDone_.clear();
}

void ProgressTracker::setPartCount(int parts)
{
    parts_ = qMax(1, parts);
    sizes_.resize(parts_);
    partDone_.resize(parts_);
}

void ProgressTracker::startPart()
{
    if (part_ >= 0 && part_ < partDone_.size())
        partDone_[part_] = 1;   // the previous file is complete
    part_ = qMin(part_ + 1, parts_ - 1);
    if (sizes_.size() < parts_) {
        sizes_.resize(parts_);
        partDone_.resize(parts_);
    }
}

void ProgressTracker::finishPart()
{
    if (part_ < 0)
        startPart();
    partDone_[part_] = 1;
    shown_ = std::max(shown_, (item_ + itemFraction()) / items_);
}

double ProgressTracker::update(qint64 done, qint64 total, int fragment, int fragments)
{
    if (part_ < 0)
        startPart();

    // Fragment counts don't change mid-download, unlike the byte estimate, so prefer them.
    double f = -1;
    if (fragments > 0 && fragment >= 0)
        f = double(fragment) / fragments;
    else if (total > 0)
        f = double(done) / total;
    if (total > 0)
        sizes_[part_] = total;
    if (f < 0)
        return shown_;

    partDone_[part_] = std::clamp(f, partDone_[part_], 1.0);
    const double overall = (item_ + itemFraction()) / items_;
    // Never move backwards, and keep the last sliver for when yt-dlp says it's finished.
    shown_ = std::max(shown_, std::min(overall, 0.999));
    return shown_;
}

qint64 ProgressTracker::knownBytes() const
{
    qint64 sum = 0;
    for (qint64 s : sizes_)
        sum += s;
    return sum;
}

double ProgressTracker::itemFraction() const
{
    double sum = 0;
    for (int p = 0; p < parts_; ++p)
        sum += partWeight(p) * partDone_.value(p);
    return std::clamp(sum, 0.0, 1.0);
}

// A part's share of the item, by size where known.
double ProgressTracker::partWeight(int part) const
{
    if (parts_ == 1)
        return 1;
    // Sizes from the preview fill in parts that haven't started (sound is sometimes the bigger one).
    if (items_ == 1 && expected_.size() == parts_) {
        double total = 0;
        for (int p = 0; p < parts_; ++p)
            total += sizes_.value(p) > 0 ? sizes_.value(p) : expected_.value(p);
        if (total > 0)
            return (sizes_.value(part) > 0 ? sizes_.value(part) : expected_.value(part)) / total;
    }
    qint64 known = 0;
    int knownCount = 0;
    for (qint64 s : sizes_)
        if (s > 0)
            known += s, ++knownCount;
    if (knownCount == 0)
        return part == 0 ? 1 - guessedLaterPartShare * (parts_ - 1) : guessedLaterPartShare;

    const double guess = double(known) / knownCount * guessedLaterPartShare;
    double total = 0;
    for (int p = 0; p < parts_; ++p)
        total += sizes_.value(p) > 0 ? sizes_.value(p) : guess;
    const double mine = sizes_.value(part) > 0 ? sizes_.value(part) : guess;
    return mine / total;
}
