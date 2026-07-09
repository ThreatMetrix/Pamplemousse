// Expected per-feature contributions for XGBoostBinaryLogistic.pmml under the
// path-attribution scheme defined in docs/adr/0002-path-attribution-for-tree-ensembles.md.
//
// Generated from the PMML by walking each tree and summing
// (score[child] - score[parent]) per edge, attributed to the field tested in
// child's predicate. __bias__ = base_score + sum_over_trees(score[root]).
// Identity verified to 1e-9: raw_score == __bias__ + sum(contribs).
//
// To regenerate, see the spike walker in docs/adr/0002 or the original
// session plan (xgb_model_F.json -> JPMML-XGBoost 1.9.8 -> this PMML).

#ifndef contributions_fixture_hpp
#define contributions_fixture_hpp

#include <string>
#include <vector>
#include <map>

namespace ContributionsFixture
{
    struct Case
    {
        std::map<std::string, double> input;
        double raw_score;
        double bias;
        std::map<std::string, double> contribs;
    };

    // Three independent inputs covering low, mid, and high feature regimes.
    inline std::vector<Case> xgboostBinaryLogisticCases()
    {
        return {
            // Mid regime
            {
                {
                    {"median_age_Fname", 35.0},
                    {"q60_age_Fname",    40.0},
                    {"q70_age_Fname",    45.0},
                    {"q80_age_Fname",    50.0},
                    {"q90_age_Fname",    60.0},
                },
                -0.9311254980,
                 0.6628086100,
                {
                    {"median_age_Fname", -0.0986579635},
                    {"q60_age_Fname",    -0.5739634920},
                    {"q70_age_Fname",    -0.3375731070},
                    {"q80_age_Fname",    -0.3819164500},
                    {"q90_age_Fname",    -0.2018230955},
                }
            },
            // Low regime
            {
                {
                    {"median_age_Fname", 25.0},
                    {"q60_age_Fname",    28.0},
                    {"q70_age_Fname",    30.0},
                    {"q80_age_Fname",    32.0},
                    {"q90_age_Fname",    35.0},
                },
                -1.0430765570,
                 0.6628086100,
                {
                    {"median_age_Fname", -0.1007572035},
                    {"q60_age_Fname",    -0.5823167510},
                    {"q70_age_Fname",    -0.3506323380},
                    {"q80_age_Fname",    -0.4106187370},
                    {"q90_age_Fname",    -0.2615601375},
                }
            },
            // High regime
            {
                {
                    {"median_age_Fname", 60.0},
                    {"q60_age_Fname",    65.0},
                    {"q70_age_Fname",    70.0},
                    {"q80_age_Fname",    75.0},
                    {"q90_age_Fname",    80.0},
                },
                -0.0320777458,
                 0.6628086100,
                {
                    {"median_age_Fname", -0.3286692105},
                    {"q60_age_Fname",    -0.2365791183},
                    {"q70_age_Fname",    -0.0764787687},
                    {"q80_age_Fname",    -0.0620421233},
                    {"q90_age_Fname",     0.0088828650},
                }
            },
        };
    }
}

#endif
