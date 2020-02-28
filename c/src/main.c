#include <stdbool.h>    // bool, false, true
#include <stddef.h>     // size_t
#include <stdio.h>      // stderr, stdout, fprintf
#include <stdint.h>     // int32_t
#include <stdlib.h>     // EXIT_*, atoi

#include <getopt.h>     // getopt

#include "htslib/vcf.h"     // hts*, bcf_*


static inline int
imax(int const a, int const b)
{
    return a >= b ? a : b;
} // imax


static inline int
imin(int const a, int const b)
{
    return a <= b ? a : b;
} // imin


int
main(int argc, char* argv[])
{
    int threshold = 10;
    int merge = 1;
    int distance = 0;
    bool nflag = false;
    bool dflag = 0;
    int err = 0;

    int opt = -1;
    while ((opt = getopt(argc, argv, "t:nd:")) != -1)
    {
        switch (opt)
        {
            case 't':
                threshold = atoi(optarg);
                break;
            case 'n':
                nflag = true;
                merge = 0;
                break;
            case 'd':
                dflag = true;
                distance = atoi(optarg);
                break;
            case '?':
                err = 1;
                break;
        } // switch
    } // while

    if (nflag && dflag)
    {
        (void) fprintf(stderr, "Both no merge and distance specified!\n");
        err += 1;
    } // if

    if (0 != err)
    {
        static char const* const usage = "usage: %s [-t threshold] [-d distance] [-n]\n";
        fprintf(stderr, usage, argv[0]);
        return EXIT_FAILURE;
    } // if

    htsFile* const fh = bcf_open("-", "r");
    if (NULL == fh)
    {
        (void) fprintf(stderr, "bcf_open() failed\n");
        return EXIT_FAILURE;
    } // if

    bcf_hdr_t* const hdr = bcf_hdr_read(fh);
    if (1 != bcf_hdr_nsamples(hdr))
    {
        (void) fprintf(stderr, "#samples = %d\n", bcf_hdr_nsamples(hdr));
        goto error1;
    } // if

    int nseq = 0;
    char const** seqnames = bcf_hdr_seqnames(hdr, &nseq);
    if (NULL == seqnames)
    {
        (void) fprintf(stderr, "bcf_hdr_seqnames() failed\n");
        goto error1;
    } // if

    bcf1_t* rec = bcf_init();
    if (NULL == rec)
    {
        (void) fprintf(stderr, "bcf_init() failed\n");
        goto error2;
    } // if

    int32_t* dp = NULL;
    int32_t* gt = NULL;

    bool first = true;
    bool jump = false;

    int window_start = 0;
    int window_end = 0;
    const char* window_chrom = NULL;
    int window_ploidy = 0;

    while (0 == bcf_read(fh, hdr, rec))
    {
        int32_t depth = 0;
        if (1 == bcf_get_format_int32(hdr, rec, "DP", &dp, &(int){0}))
        {
            depth = dp[0];
        } // if

        //
        // If depth is below the threshold, no need to proceed
        //
        if (depth < threshold)
        {
            continue;
        } // if

        int ngt_arr = 0;
        int const ploidy = bcf_get_format_int32(hdr, rec, "GT", &gt, &ngt_arr);
        if (0 > ploidy)
        {
            (void) fprintf(stderr, "bcf_get_genotypes failed\n");
            goto error;
        } // if

        if (ploidy != ngt_arr)
        {
            (void) fprintf(stderr, "ploidy != count\n");
            goto error;
        } // if

        //
        // Convenience handles
        //
        const char* chrom = seqnames[rec->rid];
        int start = rec->pos;
        int end = rec->pos + rec->rlen;

        //
        // When we don't merge, just print here and proceed
        //
        if (0 == merge)
        {
            (void) fprintf(stdout, "%s\t%d\t%d\t%d\n", chrom, start, end, ploidy);
            continue;
        } // if

        //
        // We just started
        //
        if (first)
        {
            window_start = start;
            window_end = end;
            window_chrom = chrom;
            window_ploidy = ploidy;

            // eprint(f"First! c:{window_chrom} s:{start}, w_s={window_start} e:{end} w_e={window_end}")

            first = false;
            continue;
        } // if

        if (window_chrom != chrom ||
            window_ploidy != ploidy ||
            window_end + distance < start)
        {
            jump = true;
            // eprint(f"Chrom changed from {window_chrom} to {chrom}.")
            (void) fprintf(stdout, "%s\t%d\t%d\t%d\n", window_chrom, window_start, window_end, window_ploidy);

            window_start = start;
            window_end = end;
            window_chrom = chrom;
            window_ploidy = ploidy;
        } // if
        else
        {
            window_start = imin(window_start, start);
            window_end = imax(window_end, end);
            // eprint(f"No jump! s:{start}, w_s={window_start} e:{end} w_e={window_end}")
        } // else
    } // while

    //
    // If the last iteration of the loop was not a jump, we still need to print
    //
    if (0 != merge && !jump)
    {
        (void) fprintf(stdout, "%s\t%d\t%d\t%d\n", window_chrom, window_start, window_end, window_ploidy);
    } // if

    bcf_destroy(rec);
    free(dp);
    free(gt);
    free(seqnames);
    bcf_hdr_destroy(hdr);
    bcf_close(fh);

    return EXIT_SUCCESS;

error:
    bcf_destroy(rec);
    free(dp);
    free(gt);

error2:
    free(seqnames);

error1:
    bcf_hdr_destroy(hdr);
    bcf_close(fh);

    return EXIT_FAILURE;
} // main
