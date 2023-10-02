//FastQTL: Fast and efficient QTL mapper for molecular phenotypes
//Copyright (C) 2015 Olivier DELANEAU, Alfonso BUIL, Emmanouil DERMITZAKIS & Olivier DELANEAU
//
//This program is free software: you can redistribute it and/or modify
//it under the terms of the GNU General Public License as published by
//the Free Software Foundation, either version 3 of the License, or
//(at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "data.h"
#include "utils/tabix.hpp"


void data::readGenotypesVCF(string fvcf) {
    string buffer;
    vector<string> str, field;
    int n_includedG = 0;
    int n_excludedG = 0;
    int n_excludedF = 0;
    int n_includedS = 0;
    int n_excludedS = 0;
    int n_excludedMAF = 0;
    int n_excludedMAFglobal = 0;
    int n_missingS = 0;
    int n_parsed = 0;
    vector < int > mappingS;

    //Initialise
    LOG.println("\nReading genotype data in [" + fvcf + "] in VCF format");
    if (!futils::isFile(fvcf + ".tbi")) LOG.error("index file missing [" + fvcf + ".tbi]");
    Tabix fd (fvcf);

    //Read samples
    fd.getLastHeader(buffer);
    if (buffer.size() == 0) LOG.error("No header line detected!");
    sutils::tokenize(buffer, str);
    if (str.size() < 10) LOG.error("Wrong VCF header format for sample ids");
    for (int t = 9 ; t < str.size() ; t ++) {
        if (checkSample(str[t], false)) {
            int idx_sample = -1;
            for (int i = 0 ; i < sample_count && idx_sample < 0 ; i++) if (sample_id[i] == str[t]) idx_sample = i;
            mappingS.push_back(idx_sample);
            if (idx_sample >= 0) n_includedS ++;
            else n_missingS ++;
        } else {
            mappingS.push_back(-1);
            n_excludedS ++;
        }
    }
    if (n_includedS != sample_count) LOG.error("Genotype data does not overlap with phenotype data, check your files!");

    double median_interaction = 0.0;
    if (interaction_maf_threshold>0.0) {
        // calculate median
        std::vector<float> interaction_val_sorted(interaction_val);
        std::sort(interaction_val_sorted.begin(), interaction_val_sorted.end());
        size_t s = interaction_val.size();
        if ((s%2)==0) {
            median_interaction = (interaction_val_sorted[s/2] + interaction_val_sorted[(s/2)-1]) / 2.0;
        } else {
            median_interaction = interaction_val_sorted[s/2];
        }
    }

    //Read genotypes
    if (!fd.setRegion(regionGenotype.str())) LOG.error("Failed to get region " + regionGenotype.str() + " in [" + fvcf + "]");
    LOG.println("  * region = " + regionGenotype.str());
    while (fd.getNextLine(buffer)) {
        if (buffer.size() == 0) continue;
        sutils::tokenize(buffer, str);
        if (checkGenotype(str[2])) {
            //Check VCF format
            bool gt_field = false;
            int idx_field = -1;
            sutils::tokenize(str[8], field, ":");  // FORMAT field
            for (int f = 0 ; f < field.size() ; f ++) if (field[f] == "DS") idx_field = f;  // check if DS present, else use GT
            if (idx_field < 0) { for (int f = 0 ; f < field.size() ; f ++) if (field[f] == "GT") idx_field = f; gt_field = true; }

            bool passed_AF = true;
            if (global_af_threshold>0.0) {
                // apply AF filter
                sutils::tokenize(str[7], field, ";");  // INFO field
                for (int f=0; f<field.size(); ++f) {
                    size_t p = field[f].find("=");
                    if (field[f].substr(0, p)=="AF") {
                        double val = std::stod(field[f].substr(p+1, string::npos));
                        if ((val<global_af_threshold) | (val>1.0-global_af_threshold)) {
                            passed_AF = false;
                        }
                        break;
                    }
                }
            }

            //Read data if format is correct
            if (idx_field >= 0) {
                if (passed_AF) {
                    vector < float > genotype_vec = vector < float > (sample_count, 0.0);

                    for (int t = 9 ; t < str.size() ; t ++) {
                        if (mappingS[t-9] >= 0) {  // if sample is in include list
                            sutils::tokenize(str[t], field, ":");
                            if (str[t] == "." || str[t] == "NN" || str[t] == "NA") genotype_vec[mappingS[t-9]] = -1.0;
                            else if (!gt_field) {
                                if (field[idx_field][0] == '.') genotype_vec[mappingS[t-9]] = -1.0;
                                else {
                                    float dosage = atof(field[idx_field].c_str());
                                    //if (dosage < 0 || dosage > 2) LOG.error("Dosages must be between 0 and 2, check: " + field[idx_field]);
                                    genotype_vec[mappingS[t-9]] = dosage;
                                }
                            } else {
                                if (field[idx_field][0] == '.' || field[idx_field][2] == '.') genotype_vec[mappingS[t-9]] = -1.0;
                                else {
                                    int a0 = atoi(field[idx_field].substr(0, 1).c_str());
                                    int a1 = atoi(field[idx_field].substr(2, 1).c_str());
                                    int dosage = a0 + a1;
                                    if (dosage < 0 || dosage > 2) LOG.error("Genotypes must be 00, 01, or 11, check: " + field[idx_field]);
                                    genotype_vec[mappingS[t-9]] = dosage;
                                }
                            }
                        }
                    }
                    // calculate minor allele frequency
                    // for now, replicate previous approach
                    // in the future, use alt_alleles = sum(dosages)
                    int c0 = 0;
                    int c1 = 0;
                    int c2 = 0;
                    int r;
                    for (int i=0; i<genotype_vec.size(); ++i) {
                        if (genotype_vec[i]!=-1.0) {
                            r = round(genotype_vec[i]);
                            if (r==0) {
                                c0++;
                            } else if (r==1) {
                                c1++;
                            } else if (r==2) {
                                c2++;
                            } else {
                                LOG.error("Dosage values must be between 0 and 2");
                            }
                        }
                    }
                    float ref_alleles = 2*c0 + c1;
                    float alt_alleles = c1 + 2*c2;
                    float maf;
                    int ma_samples;
                    int ma_count;
                    int num_samples = c0+c1+c2;
                    int ref_factor;
                    if (ref_alleles >= alt_alleles) {
                        maf = alt_alleles / (float)(2*num_samples);
                        ma_samples = c1+c2;
                        ma_count = alt_alleles;
                        ref_factor = 1;
                    } else {  // ref is minor allele
                        maf = ref_alleles / (float)(2*num_samples);
                        ma_samples = c0+c1;
                        ma_count = ref_alleles;
                        ref_factor = -1;
                    }

                    std::map<int,int> c_upper;
                    std::map<int,int> c_lower;
                    float maf_upper = 0.0;
                    float maf_lower = 0.0;
                    if (interaction_maf_threshold>0.0) {
                        // calculate MAF in each half
                        for (int i=0; i<genotype_vec.size(); ++i) {
                            if (genotype_vec[i]!=-1.0) {
                                r = round(genotype_vec[i]);
                                if (interaction_val[i] >= median_interaction) {
                                    c_upper[r]++;
                                } else {
                                    c_lower[r]++;
                                }
                            }
                        }
                        float alt_upper = c_upper[1] + 2*c_upper[2];
                        maf_upper = alt_upper / (float)(2*(c_upper[0]+c_upper[1]+c_upper[2]));
                        if (maf_upper>0.5) {
                            maf_upper = 1.0 - maf_upper;
                        }
                        float alt_lower = c_lower[1] + 2*c_lower[2];
                        maf_lower = alt_lower / (float)(2*(c_lower[0]+c_lower[1]+c_lower[2]));
                        if (maf_lower>0.5) {
                            maf_lower = 1.0 - maf_lower;
                        }
                    }

                    if (maf>=maf_threshold &&
                        ma_samples>=ma_sample_threshold &&
                        maf_lower>=interaction_maf_threshold &&
                        maf_upper>=interaction_maf_threshold) {
                        genotype_id.push_back(str[2]);
                        genotype_chr.push_back(str[0]);
                        genotype_pos.push_back(atoi(str[1].c_str()));
                        genotype_orig.push_back(genotype_vec);
                        genotype_curr.push_back(vector < float > (sample_count, 0.0));
                        genotype_maf.push_back(maf);
                        genotype_ma_count.push_back(ma_count);
                        genotype_ma_samples.push_back(ma_samples);
                        genotype_ref_factor.push_back(ref_factor);
                        n_includedG++;
                    } else {
                        n_excludedMAF++;
                    }
                } else n_excludedMAFglobal++;
            } else n_excludedF++;
        } else n_excludedG++;
        n_parsed++;
        if (n_parsed % 100000 == 0) LOG.println("  * " + sutils::int2str(n_parsed) + " lines parsed");
    }

    //Finalise
    genotype_count = n_includedG;
    //LOG.println("  * region = " + regionGenotype.str());
    LOG.println("  * " + sutils::int2str(n_includedS) + " samples included");
    if (n_excludedS > 0) LOG.println("  * " + sutils::int2str(n_excludedS) + " samples excluded");
    if (n_missingS > 0) LOG.println("  * " + sutils::int2str(n_excludedS) + " samples excluded without phenotype data");
    LOG.println("  * " + sutils::int2str(n_includedG) + " sites included");
    if (n_excludedG > 0) LOG.println("  * " + sutils::int2str(n_excludedG) + " sites excluded");
    if (n_excludedF > 0) LOG.println("  * " + sutils::int2str(n_excludedF) + " sites excluded because of missing GT/DS field");
    if (n_excludedMAF > 0) LOG.println("  * " + sutils::int2str(n_excludedMAF) + " sites excluded because below minor allele thresholds for selected samples");
    if (n_excludedMAFglobal > 0) LOG.println("  * " + sutils::int2str(n_excludedMAFglobal) + " sites excluded because global minor allele frequency < " + sutils::double2str(global_af_threshold));
    if (n_includedG <= 0) LOG.error("No genotypes in this region: " + regionPhenotype.str());
}
