// clang-format off
//!
//! Animator application.
//!
//! Shows an animation of a mesh by interpolating a set of poses.
//!
//! Usage:
//!   animator [-d|--duration <duration (s)>] [-f|--fps <frames per second>] <pose files>...
//!
//! \author Javier Dehesa (javidcf@gmail.com)
//!
// clang-format on


#include <QApplication> 
#include <QCommandLineParser>
#include <QStringList>

#include <iostream>
#include <memory>
#include <tuple>

#include "animator.h"

//!
//! \brief Process command line arguments.
//!
//! \param app QT application
//!
//! \return Poses file paths, animation duration and frame rate
//!
std::tuple<QStringList, float, float, bool, QString>
processArgs(const QApplication &app);

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Animator");
    QApplication::setApplicationVersion("1.0");

    QStringList files;
    float duration;
    float fps;
    bool repair;
    QString envpath;
    std::tie(files, duration, fps, repair, envpath) = processArgs(app);

    Animator animator(files, duration, fps, repair, envpath);
    animator.show();

    return app.exec();
}

std::tuple<QStringList, float, float, bool, QString>
processArgs(const QApplication &app)
{
    QCommandLineParser parser;
    parser.setApplicationDescription("Animates a mesh by interpolating "
                                     "multiple poses.");

    parser.addPositionalArgument("pose files", "Mesh pose files (2 or more).",
                                 "<pose files...>");

    parser.addOptions({
        {{"d", "duration"}, "Duration of the animation (default: number of poses minus one).", "seconds"},
        {{"f", "fps"}, "Frames per second (default: 25).", "fps", "25"},
        {{"r", "repair"}, "Enable mesh repair on each interpolated frame."},
        {{"e", "env"}, "Location of the env file."},
    });

    parser.addHelpOption();
    parser.addVersionOption();

    parser.process(app);

    const QStringList files = parser.positionalArguments();
    if (files.size() < 2)
    {
        parser.showHelp(1);
    }

    bool valid;
    float duration = files.size() - 1.0f;
    if (parser.isSet("duration"))
    {
        duration = parser.value("duration").toDouble(&valid);
        if (!valid || duration <= 0.0f)
        {
            parser.showHelp(1);
        }
    }

    float fps = parser.value("fps").toDouble(&valid);
    if (!valid || fps <= 0.0f)
    {
        parser.showHelp(1);
    }
    // at the end of processArgs:
    bool repair = parser.isSet("repair");

    QString envPath;
    if (parser.isSet("env"))
    {
        envPath = parser.value("env");
    }
    else
    {
        envPath = QCoreApplication::applicationDirPath() + "/../.env";
    }

    return std::make_tuple(files, duration, fps, repair, envPath);
}