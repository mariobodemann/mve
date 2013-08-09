#include <iomanip>
#include <iostream>
#include <cstdlib>
#include <csignal>

#include "dmrecon/settings.h"
#include "dmrecon/dmrecon.h"
#include "mve/scene.h"
#include "mve/view.h"
#include "util/arguments.h"
#include "util/system.h"
#include "util/tokenizer.h"

#include "fancy_progress_printer.h"

enum ProgressStyle
{
    PROGRESS_SILENT,
    PROGRESS_SIMPLE,
    PROGRESS_FANCY
};

FancyProgressPrinter fancyProgressPrinter;

void
reconstruct (mve::Scene::Ptr scene, mvs::Settings settings)
{
    /* Note: destructor of ProgressHandle sets status to failed
       if setDone() is not called (this happens when an exception
       is thrown in mvs::DMRecon) */
    ProgressHandle handle(fancyProgressPrinter, settings);
    mvs::DMRecon recon(scene, settings);
    handle.setRecon(recon);
    recon.start();
    handle.setDone();
}

int
main (int argc, char** argv)
{
    /* Catch segfaults to print stack traces. */
    ::signal(SIGSEGV, util::system::signal_segfault_handler);

    /* Parse arguments. */
    util::Arguments args;
    args.set_usage(argv[0], "[ OPTIONS ] SCENEDIR");
    args.set_helptext_indent(23);
    args.set_nonopt_minnum(1);
    args.set_nonopt_maxnum(1);
    args.set_exit_on_error(true);
    args.add_option('n', "neighbors", true,
        "amount of neighbor views (global view selection)");
    args.add_option('m', "master-view", true,
        "reconstructs given master view ID only");
    args.add_option('l', "list-view", true,
        "reconstructs given view IDs (given as string \"0-10\")");
    args.add_option('s', "scale", true,
        "reconstruction on given scale (0 is original size)");
    args.add_option('f', "filter-width", true,
        "patch size for NCC based comparison (default is 5)");
    args.add_option('\0', "nocolorscale", false,
        "turn off color scale");
    args.add_option('i', "image", true,
        "specify image embedding used in reconstruction");
    args.add_option('\0', "keep-dz", false,
        "store dz map into view");
    args.add_option('\0', "keep-conf", false,
        "store confidence map into view");
    args.add_option('p', "writeply", false,
        "use this option to write the ply file");
    args.add_option('\0', "plydest", true,
        "path suffix appended to scene dir to write ply files");
    args.add_option('\0', "logdest", true,
        "path suffix appended to scene dir to write log files");
    args.add_option('\0', "progress", true,
        "progress output style: 'silent', 'simple' or 'fancy'");
    args.add_option('\0', "force", false, "Re-reconstruct existing depthmaps");
    args.parse(argc, argv);

    std::string basePath;
    bool writeply = false;
    std::string plyDest("/recon");
    std::string logDest("/log");
    int master_id = -1;
    bool force_recon = false;
    ProgressStyle progress_style;

#ifdef _WIN32
    progress_style = PROGRESS_SIMPLE;
#else
    progress_style = PROGRESS_FANCY;
#endif

    mvs::Settings mySettings;
    std::vector<int> listIDs;

    util::ArgResult const * arg;
    while ((arg = args.next_result()))
    {
        if (arg->opt == NULL)
        {
            basePath = arg->arg;
            continue;
        }

        if (arg->opt->lopt == "neighbors")
        {
            mySettings.globalVSMax =
                util::string::convert<std::size_t>(arg->arg);
            std::cout << "global view selection uses "
                      << mySettings.globalVSMax
                      << " neighbors" << std::endl;
        }
        else if (arg->opt->lopt == "nocolorscale")
            mySettings.useColorScale = false;
        else if (arg->opt->lopt == "master-view")
            master_id = arg->get_arg<int>();
        else if (arg->opt->lopt == "list-view")
            args.get_ids_from_string(arg->arg, listIDs);
        else if (arg->opt->lopt == "scale")
            mySettings.scale = arg->get_arg<int>();
        else if (arg->opt->lopt == "filter-width")
            mySettings.filterWidth = arg->get_arg<unsigned int>();
        else if (arg->opt->lopt == "image")
            mySettings.imageEmbedding = arg->get_arg<std::string>();
        else if (arg->opt->lopt == "keep-dz")
            mySettings.keepDzMap = true;
        else if (arg->opt->lopt == "keep-conf")
            mySettings.keepConfidenceMap = true;
        else if (arg->opt->lopt == "writeply")
            writeply = true;
        else if (arg->opt->lopt == "plydest")
            plyDest = arg->arg;
        else if (arg->opt->lopt == "logdest")
            logDest = arg->arg;
        else if (arg->opt->lopt == "force")
            force_recon = true;
        else if (arg->opt->lopt == "progress")
        {
            if (arg->arg == "silent")
                progress_style = PROGRESS_SILENT;
            else if (arg->arg == "simple")
                progress_style = PROGRESS_SIMPLE;
            else if (arg->arg == "fancy")
                progress_style = PROGRESS_FANCY;
            else
                std::cout << "WARNING: unrecognized progress style" << std::endl;
        }
        else {
            std::cout << "WARNING: unrecognized option" << std::endl;
        }
    }

    /* don't show progress twice */
    if (progress_style != PROGRESS_SIMPLE)
        mySettings.quiet = true;

    /* Load MVE scene. */
    mve::Scene::Ptr scene(mve::Scene::create());
    try {
        scene->load_scene(basePath);
        scene->get_bundle();
    }
    catch (std::exception& e) {
        std::cout<<"Error loading scene: "<<e.what()<<std::endl;
        return 1;
    }

    /* Settings for Multi-view stereo */
    mySettings.writePlyFile = writeply; // every time this is set to true, a kitten is killed
    mySettings.plyPath = basePath;
    mySettings.plyPath += "/";
    mySettings.plyPath += plyDest;
    mySettings.plyPath += "/";
    mySettings.logPath = basePath;
    mySettings.plyPath += "/";
    mySettings.logPath += logDest;
    mySettings.logPath += "/";

    fancyProgressPrinter.setBasePath(basePath);
    fancyProgressPrinter.setNumViews(scene->get_views().size());

    if (progress_style == PROGRESS_FANCY)
        fancyProgressPrinter.pt_create();

    if (master_id >= 0) {
        std::cout << "Reconstructing view with ID " << master_id << std::endl;
        mySettings.refViewNr = (std::size_t)master_id;
        fancyProgressPrinter.addRefView(master_id);
        try
        {
            reconstruct(scene, mySettings);
        }
        catch (std::exception &err)
        {
            std::cerr << err.what() << std::endl;
        }
    }
    else
    {
        mve::Scene::ViewList& views(scene->get_views());
        std::string embedding_name = "depth-L" +
            util::string::get(mySettings.scale);
        if (listIDs.empty()) {
            for(std::size_t i = 0; i < views.size(); ++i)
                listIDs.push_back(i);
            std::cout << "Reconstructing all views..." << std::endl;
        }
        else {
            std::cout << "Reconstructing views from list..." << std::endl;
        }
        fancyProgressPrinter.addRefViews(listIDs);

#pragma omp parallel for schedule(dynamic, 1)
        for (std::size_t i = 0; i < listIDs.size(); ++i)
        {
            std::size_t id = listIDs[i];
            if (id >= views.size()){
                std::cout << "ID: " << id << " is too large! Skipping..."
                << std::endl;
                continue;
            }
            if (views[id] == NULL || !views[id]->is_camera_valid())
                continue;
            if (!force_recon && views[id]->has_embedding(embedding_name))
                continue;

            mvs::Settings settings(mySettings);
            settings.refViewNr = id;
            try
            {
                reconstruct(scene, settings);
                views[id]->save_mve_file();
            }
            catch (std::exception &err)
            {
                std::cerr << err.what() << std::endl;
            }
        }
    }

    if (progress_style == PROGRESS_FANCY)
    {
        fancyProgressPrinter.stop();
        fancyProgressPrinter.pt_join();
    }

    /* Save scene */
    std::cout << "Saving views back to disc..." << std::endl;
    scene->save_views();

    return 0;
}
