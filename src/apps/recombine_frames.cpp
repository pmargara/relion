
#include <unistd.h>
#include <string.h>
#include <fstream>

#include <src/args.h>
#include <src/image.h>
#include <src/fftw.h>
#include <src/complex.h>
#include <src/metadata_table.h>
#include <src/backprojector.h>
#include <src/euler.h>
#include <src/jaz/vtk_helper.h>
#include <src/jaz/slice_helper.h>
#include <src/jaz/spectral_helper.h>
#include <src/jaz/filter_helper.h>
#include <src/jaz/backprojection_helper.h>
#include <src/jaz/volume_converter.h>
#include <src/jaz/complex_io.h>
#include <src/jaz/fftw_helper.h>
#include <src/jaz/resampling_helper.h>
#include <src/jaz/ctf_helper.h>
#include <src/jaz/defocus_refinement.h>
#include <src/jaz/magnification_refinement.h>
#include <src/jaz/refinement_helper.h>
#include <src/jaz/stack_helper.h>
#include <src/jaz/tilt_refinement.h>
#include <src/jaz/motion_refinement.h>
#include <src/jaz/image_op.h>
#include <src/jaz/Fourier_helper.h>
#include <src/jaz/fsc_helper.h>
#include <src/jaz/damage_helper.h>
#include <src/jaz/interpolation.h>
#include <src/jaz/distribution_helper.h>
#include <src/jaz/noise_helper.h>
#include <src/jaz/convolution_helper.h>
#include <src/jaz/motion_em.h>
#include <src/jaz/local_motion_fit.h>
#include <src/jaz/gradient_descent.h>
#include <src/jaz/refinement_program.h>
#include <src/jaz/parallel_ft.h>

#include <omp.h>

using namespace gravis;

class FrameRecomb : public RefinementProgram
{
    public:

        FrameRecomb();

            RFLOAT dmga, dmgb, dmgc, totalDose;

            bool perMgBFacs, globBfac, hybridBfac,
                dataWeight, writeNumbers, fccOnly, writeWeights, eval,
                hasRef, hasCC;

            int k0, k1;

            std::string trackFn, fccFn;

        int readMoreOptions(IOParser& parser, int argc, char *argv[]);
        int _init();
        int _run();
};

int main(int argc, char *argv[])
{
    FrameRecomb mt;

    int rc0 = mt.init(argc, argv);
    if (rc0 != 0) return rc0;

    int rc1 = mt.run();
    if (rc1 != 0) return rc1;
}

FrameRecomb::FrameRecomb()
: RefinementProgram(false, true)
{}

int FrameRecomb::readMoreOptions(IOParser& parser, int argc, char *argv[])
{
    trackFn = parser.getOption("--t", "Input tracks", "");
    fccFn = parser.getOption("--cc", "Input MRC file of per-micrograph Fourier cross-correlations (for global B-factors)", "");
    writeWeights = parser.checkOption("--write_weights", "Write out freq. weights instead of normalizing them");

    k0 = textToInteger(parser.getOption("--k0", "Min. frequency used in B-factor fit", "20"));
    k1 = textToInteger(parser.getOption("--k1", "Max. frequency used in B-factor fit", "100"));

    dmga = textToFloat(parser.getOption("--dmg_a", "Damage model, parameter a", " 2.9"));
    dmgb = textToFloat(parser.getOption("--dmg_b", "                        b", "-1.1"));
    dmgc = textToFloat(parser.getOption("--dmg_c", "                        c", "-1.2"));

    totalDose = textToFloat(parser.getOption("--dose", "Total electron dose (in e^-/A^2)", "0"));

    dataWeight = parser.checkOption("--data_weight", "Use FCC as a weight");
    writeNumbers = parser.checkOption("--write_numbers", "Write B/k-factors to bfacs/");
    fccOnly = parser.checkOption("--fcc_only", "Only compute the global FCC");
    eval = parser.checkOption("--eval", "Evaluate recombined images");
    globBfac = parser.checkOption("--global", "Use global B/k-factors");
    hybridBfac = parser.checkOption("--hybrid", "Use hybrid B/k-factors");

    hasCC = (fccFn != "");
    hasRef = (reconFn0 != "" && reconFn1 != "");

    if (!hasCC && !hasRef)
    {
        std::cout << "Either a CC map for global B-factors (--cc) or an initial reconstruction for per-micrograph B-factors (--m) is required.\n";
        return 666;
    }

    if (eval && !hasRef)
    {
        std::cout << "A reference is required for evaluation (--eval).\n";
        return 667;
    }

    if (globBfac && !hasCC)
    {
        std::cout << "An FCC is required for global B/k-factors (--global).\n";
        return 667;
    }

    if (hybridBfac && !(hasCC && hasRef))
    {
        std::cout << "Both an FCC and a reference are required for hybrid B/k-factors (--hybrid).\n";
        return 667;
    }

    noReference = !hasRef;

    return 0;
}

int FrameRecomb::_init()
{
    return 0;
}

int FrameRecomb::_run()
{
    /*{
        std::string tag = "0-1529";
        Image<RFLOAT> fcc;
        //fcc.read("tracks_safe/all_FCC_data.mrc");
        fcc.read("bfacs/FCC_total_"+tag+".mrc");
        std::vector<d2Vector> bFacs = DamageHelper::fitBkFactors(fcc, 25, 192).first;

        std::ofstream bOut("bfacs/fcc_new_"+tag+"_B_sh.dat");
        std::ofstream kOut("bfacs/fcc_new_"+tag+"_k_sh.dat");

        for (int i = 0; i < bFacs.size(); i++)
        {
            double s = bFacs[i].x;
            double b = -119666.147328/(s*s);
            bOut << i << " " << b << "\n";
            kOut << i << " " << bFacs[i].y << "\n";
        }

        return 0;
    }*/
    /*{
        Image<RFLOAT> fcc;
        fcc.read("tracks_safe/all_FCC_data.mrc");
        std::vector<double> bFacs = DamageHelper::fitBFactors(fcc, 10, 100);

        std::ofstream bOut("bfacs/fcc_red30_B_pure.dat");

        for (int i = 0; i < bFacs.size(); i++)
        {
            double s = bFacs[i];
            double b = -119666.147328/(s*s);
            bOut << i << " " << b << "\n";
        }

        return 0;
    }*/

    Image<RFLOAT> fcc;

    std::vector<Image<RFLOAT>> freqWeights;
    Image<RFLOAT> dmgTable, wghTable;

    if (hasCC)
    {
        fcc.read(fccFn);        

        sh = fcc.data.xdim;
        s = 2 * (sh-1);
        fc = fcc.data.ydim;

        if (dataWeight)
        {
            freqWeights = DamageHelper::computeWeights(fcc);

            if (debug) VtkHelper::write(freqWeights, "bfacs/data_weightsVec.vtk");
        }
        else
        {
            std::pair<std::vector<d2Vector>,std::vector<double>> bkFacs
                    = DamageHelper::fitBkFactors(fcc, k0, k1);

            freqWeights = DamageHelper::computeWeights(bkFacs.first, sh);

            if (debug)
            {
                Image<RFLOAT> bfacFit = DamageHelper::renderBkFit(bkFacs, sh, fc);
                Image<RFLOAT> bfacFitNoScale = DamageHelper::renderBkFit(bkFacs, sh, fc, true);

                VtkHelper::writeVTK(bfacFit, "bfacs/glob_Bk-fit.vtk");
                VtkHelper::writeVTK(bfacFitNoScale, "bfacs/glob_Bk-fit_noScale.vtk");
                VtkHelper::writeVTK(fcc, "bfacs/glob_Bk-data.vtk");
                VtkHelper::write(freqWeights, "bfacs/freqWeights.vtk");
            }

        }
    }

    std::string name, fullName, movieName;
    mdts[0].getValue(EMDL_IMAGE_NAME, fullName, 0);
    mdts[0].getValue(EMDL_MICROGRAPH_NAME, movieName, 0);
    name = fullName.substr(fullName.find("@")+1);

    if (preextracted)
    {
        std::string finName;

        if (imgPath == "")
        {
            finName = name;
        }
        else
        {
            finName = imgPath + "/" + movieName.substr(movieName.find_last_of("/")+1);
        }

        Image<RFLOAT> stack0;
        stack0.read(finName, false);

        const int pc0 = mdts[0].numberOfObjects();
        const bool zstack = stack0.data.zdim > 1;
        const int stackSize = zstack? stack0.data.zdim : stack0.data.ndim;

        fc = stackSize / pc0;

        s = stack0.data.xdim;
        sh = s/2 + 1;
    }
    else
    {
        std::vector<std::vector<Image<Complex>>> movie = StackHelper::extractMovieStackFS(
            &mdts[0], meta_path, imgPath, bin, coords_bin, movie_bin, s,
            nr_omp_threads, !nogain, binType, false, hotCutoff, debug);

        fc = movie[0].size();
    }


    Image<RFLOAT> totTable(sh,fc), totWeight0(sh,fc), totWeight1(sh,fc);
    totTable.data.initZeros();
    totWeight0.data.initZeros();
    totWeight1.data.initZeros();

    if (!globBfac)
    {
        dmgTable = Image<RFLOAT>(sh,fc);

        wghTable = Image<RFLOAT>(sh,fc);
        wghTable.data.initConstant(1.0);

        if (totalDose > 0.0)
        {
            std::cout << "using damage model\n";
            for (int f = 0; f < fc; f++)
            {
                const double dose = f*totalDose/fc;

                for (int k = 0; k < sh; k++)
                {
                    dmgTable(f,k) = DamageHelper::damage(k, sh, angpix, dose, dmga, dmgb, dmgc);
                }
            }
        }
        else
        {
            std::cout << "not using damage model\n";
            dmgTable.data.initConstant(1.0);
        }
    }

    std::cout << "fc = " << fc << "\n";
    std::cout << "mg range: " << g0 << ".." << gc << "\n";

    std::vector<ParFourierTransformer> fts(nr_omp_threads);
    std::vector<Image<RFLOAT>> tables(nr_omp_threads),
            weights0(nr_omp_threads), weights1(nr_omp_threads);

    std::vector<std::vector<double>> finalFCC(nr_omp_threads),
            finalFCC_w0(nr_omp_threads), finalFCC_w1(nr_omp_threads);

    for (int i = 0; i < nr_omp_threads; i++)
    {
        finalFCC[i] = std::vector<double>(sh,0.0);
        finalFCC_w0[i] = std::vector<double>(sh,0.0);
        finalFCC_w1[i] = std::vector<double>(sh,0.0);
    }

    double t0 = omp_get_wtime();

    for (long g = g0; g <= gc; g++)
    {
        std::cout << "micrograph " << g << " / " << mdts.size() <<"\n";

        std::stringstream stsg;
        stsg << g;

        const int pc = mdts[g].numberOfObjects();

        std::vector<std::vector<Image<Complex> > > movie;

        try
        {
            if (preextracted)
            {
                movie = StackHelper::loadMovieStackFS(
                    &mdts[g], imgPath, false, nr_omp_threads, &fts);
            }
            else
            {
                movie = StackHelper::extractMovieStackFS(
                    &mdts[g], meta_path, imgPath,
                    bin, coords_bin, movie_bin, s,
                    nr_omp_threads, !nogain, binType,
                    true, hotCutoff, debug);

                #pragma omp parallel for num_threads(nr_omp_threads)
                for (int p = 0; p < pc; p++)
                {
                    StackHelper::varianceNormalize(movie[p], false);
                }
            }
        }
        catch (RelionError XE)
        {
            std::cerr << "warning: unable to load micrograph #" << (g+1) << "\n";
            continue;
        }

        const int sh = movie[0][0]().xdim;
        const int s = movie[0][0]().ydim;

        std::string tfn = trackFn + "_mg" + stsg.str() + "_tracks.dat";
        std::ifstream trackIn(tfn);

        std::vector<std::vector<d2Vector>> shift(pc);

        for (int p = 0; p < pc; p++)
        {
            if (!trackIn.good())
            {
                std::cerr << "relion_recombine_frames: error reading tracks in " << tfn << "\n";
                return 5;
            }

            shift[p] = std::vector<d2Vector>(fc);

            char dummy[4069];
            trackIn.getline(dummy, 4069);

            for (int f = 0; f < fc; f++)
            {
                char dummy[4069];
                trackIn.getline(dummy, 4069);

                std::istringstream sts(dummy);

                sts >> shift[p][f].x;
                sts >> shift[p][f].y;
            }

            trackIn.getline(dummy, 4069);
        }

        std::string imgName;
        mdts[g].getValue(EMDL_IMAGE_NAME, imgName, 0);
        imgName = outPath + "/" + imgName.substr(imgName.find_last_of("/")+1);

        std::vector<Image<RFLOAT>> predWghTh(nr_omp_threads);

        if (!globBfac)
        {
            #pragma omp parallel for num_threads(nr_omp_threads)
            for (int i = 0; i < nr_omp_threads; i++)
            {
                FscHelper::initFscTable(sh, fc, tables[i], weights0[i], weights1[i]);
                predWghTh[i] = Image<RFLOAT>(sh,s);
                predWghTh[i].data.initZeros();
            }

            #pragma omp parallel for num_threads(nr_omp_threads)
            for (int p = 0; p < pc; p++)
            {
                int threadnum = omp_get_thread_num();

                Image<Complex> obs(sh,s), pred;

                int randSubset;
                mdts[g].getValue(EMDL_PARTICLE_RANDOM_SUBSET, randSubset, p);
                randSubset -= 1;

                pred = obsModel.predictObservation(projectors[randSubset], mdts[g], p, true, true);

                CTF ctf;
                ctf.read(mdts[g], mdts[g], p);

                Image<RFLOAT> ctfImg(sh,s);
                ctf.getFftwImage(ctfImg.data, s, s, angpix);

                for (int f = 0; f < fc; f++)
                {
                    shiftImageInFourierTransform(movie[p][f](), obs(), s, -shift[p][f].x, -shift[p][f].y);

                    for (int y = 0; y < s; y++)
                    for (int x = 0; x < sh; x++)
                    {
                        predWghTh[threadnum](y,x) += ctfImg(y,x)*ctfImg(y,x);
                    }

                    /*FscHelper::updateFscTable(obs, ctfImg, f, pred, tables[threadnum],
                                              weights0[threadnum], weights1[threadnum]);*/
                    FscHelper::updateFscTable(obs, ctfImg, f, pred, tables[threadnum],
                        weights0[threadnum], weights1[threadnum]);
                }

            } // all particles

            Image<RFLOAT> table, weight;

            FscHelper::mergeFscTables(tables, weights0, weights1, table, weight);

            for (int th = 0; th < nr_omp_threads; th++)
            for (int f = 0; f < fc; f++)
            for (int x = 1; x < sh; x++)
            {
                DIRECT_A2D_ELEM(totTable.data, f, x) += DIRECT_A2D_ELEM(tables[th].data, f, x);
                DIRECT_A2D_ELEM(totWeight0.data, f, x) += DIRECT_A2D_ELEM(weights0[th].data, f, x);
                DIRECT_A2D_ELEM(totWeight1.data, f, x) += DIRECT_A2D_ELEM(weights1[th].data, f, x);
            }

            if (fccOnly) continue;

            std::vector<d2Vector> bkFacs
                    = DamageHelper::fitBkFactors(table, dmgTable, weight, k0, k1);

            if (writeNumbers)
            {
                std::ofstream bOut("bfacs/"+stsg.str()+"_B-vals.dat");
                std::ofstream sOut("bfacs/"+stsg.str()+"_sig-vals.dat");
                std::ofstream kOut("bfacs/"+stsg.str()+"_k-vals.dat");

                for (int i = 0; i < fc; i++)
                {
                    double sig = bkFacs[i].x;
                    double b = 8*(angpix*angpix*sh*sh)/(sig*sig);
                    bOut << i << " " << -b << "\n";
                    sOut << i << " " << sig << "\n";
                    kOut << i << " " << bkFacs[i].y << "\n";
                }
            }

            for (int f = 0; f < fc; f++)
            {
                if (bkFacs[f].x <= k0 || bkFacs[f].y < 0.0)
                {
                    bkFacs[f].y = 0.0;
                }
            }

            if (debug)
            {
                Image<RFLOAT> bfacFitNoScale = DamageHelper::renderBkFit(bkFacs, sh, fc);

                Image<RFLOAT> bfacFitNoScaleDmg;
                ImageOp::multiply(bfacFitNoScale, dmgTable, bfacFitNoScaleDmg);

                VtkHelper::writeVTK(bfacFitNoScaleDmg, "bfacs/"+stsg.str()+"_Bk-fit_dmgScale.vtk");
                VtkHelper::writeVTK(bfacFitNoScale, "bfacs/"+stsg.str()+"_Bk-fit_noScale.vtk");
                VtkHelper::writeVTK(table, "bfacs/"+stsg.str()+"_Bk-data.vtk");
                VtkHelper::writeVTK(weight, "bfacs/"+stsg.str()+"_Bk-FCCweight.vtk");
            }

            freqWeights = DamageHelper::computeWeights(bkFacs, sh, angpix, totalDose,
                                                       dmga, dmgb, dmgc, !writeWeights);

            if (writeWeights)
            {
                Image<RFLOAT> weightSum(sh,s);

                for (int y = 0; y < s; y++)
                for (int x = 0; x < sh; x++)
                {
                    weightSum(y,x) = 0.0;

                    for (int f = 0; f < fc; f++)
                    {
                        weightSum(y,x) += freqWeights[f](y,x) * freqWeights[f](y,x);
                    }
                }

                std::string wghName = imgName;
                wghName = wghName.substr(0, wghName.find_last_of('.')) + "_weight.mrc";

                weightSum.write(wghName);

                if (debug)
                {
                    VtkHelper::writeVTK(weightSum, wghName+".vtk");
                }
            }
        }

        Image<RFLOAT> stack(s,s,1,pc);

        #pragma omp parallel for num_threads(nr_omp_threads)
        for (int p = 0; p < pc; p++)
        {
            int threadnum = omp_get_thread_num();

            Image<Complex> sum(sh,s);
            sum.data.initZeros();

            Image<Complex> obs(sh,s);

            for (int f = 0; f < fc; f++)
            {
                shiftImageInFourierTransform(movie[p][f](), obs(), s, -shift[p][f].x, -shift[p][f].y);

                for (int y = 0; y < s; y++)
                for (int x = 0; x < sh; x++)
                {
                    sum(y,x) += freqWeights[f](y,x) * obs(y,x);
                }
            }

            if (eval)
            {
                Image<Complex> pred;

                int randSubset;
                mdts[g].getValue(EMDL_PARTICLE_RANDOM_SUBSET, randSubset, p);
                randSubset -= 1;

                pred = obsModel.predictObservation(projectors[1-randSubset], mdts[g], p, true, true);

                CTF ctf;
                ctf.read(mdts[g], mdts[g], p);

                Image<RFLOAT> ctfImg(sh,s);
                ctf.getFftwImage(ctfImg.data, s, s, angpix);

                for (int y = 0; y < s; y++)
                for (int x = 0; x < sh; x++)
                {
                    double yy = y < sh? y : y - s;

                    int idx = ROUND(sqrt(x*x + yy*yy));

                    if (idx < sh)
                    {
                        Complex z0 = ctfImg(y,x) * pred(y,x);
                        Complex z1 = ctfImg(y,x) * sum(y,x);

                        finalFCC[threadnum][idx] += z0.real * z1.real + z0.imag * z1.imag;
                        finalFCC_w0[threadnum][idx] += z0.norm();
                        finalFCC_w1[threadnum][idx] += z1.norm();
                    }
                }
            }

            Image<RFLOAT> real(s,s);

            fts[threadnum].inverseFourierTransform(sum(), real());

            for (int y = 0; y < s; y++)
            for (int x = 0; x < s; x++)
            {
                DIRECT_NZYX_ELEM(stack(), p, 0, y, x) = real(y,x);
            }
        }

        stack.write(imgName);

        if (debug)
        {
            VtkHelper::writeTomoVTK(stack, imgName+".vtk");
        }

    } // micrographs

    for (int f = 0; f < fc; f++)
    for (int x = 0; x < sh; x++)
    {
        RFLOAT w1 = DIRECT_A2D_ELEM(totWeight0.data, f, x);
        RFLOAT w2 = DIRECT_A2D_ELEM(totWeight1.data, f, x);
        RFLOAT ww = sqrt(w1 * w2);

        if (ww > 0.0)
        {
            DIRECT_A2D_ELEM(totTable.data, f, x) /= ww;
        }
    }

    std::stringstream stsg0;
    stsg0 << g0;

    std::stringstream stsg1;
    stsg1 << gc;

    if (eval)
    {
        for (int t = 1; t < nr_omp_threads; t++)
        for (int i = 0; i < sh; i++)
        {
            finalFCC[0][i] += finalFCC[t][i];
            finalFCC_w0[0][i] += finalFCC_w0[t][i];
            finalFCC_w1[0][i] += finalFCC_w1[t][i];
        }

        std::ofstream evalOut("bfacs/eval_"+stsg0.str()+"-"+stsg1.str()+".dat");

        for (int i = 0; i < sh; i++)
        {
            double w0 = finalFCC_w0[0][i];
            double w1 = finalFCC_w1[0][i];

            if (w0 > 0.0 && w1 > 0.0)
            {
                finalFCC[0][i] /= sqrt(w0*w1);
            }

            evalOut << i << " " << finalFCC[0][i] << "\n";
        }
    }

    VtkHelper::writeVTK(totTable, "bfacs/FCC_total_"+stsg0.str()+"-"+stsg1.str()+".vtk");
    totTable.write("bfacs/FCC_total_"+stsg0.str()+"-"+stsg1.str()+".mrc");

    double t1 = omp_get_wtime();
    double diff = t1 - t0;
    std::cout << "elapsed (total): " << diff << " sec\n";
}
