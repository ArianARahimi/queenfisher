#pragma once

#include <QString>

// Creates the folder if needed and, on Linux and macOS, leaves it readable by this user only:
// download history, sign-in sites and partial downloads aren't other accounts' business.
// Windows profile folders are per user already. False if the folder can't be made private.
bool makePrivateFolder(const QString &dir);
