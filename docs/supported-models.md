# Supported PMML models

Pamplemousse converts PMML 4.x models into Lua. This document lists the
PMML element types the project has unit-test coverage for, with the
minimum amount of context an ML engineer needs to get from a trained
Python model to a runnable Lua script.

## End-to-end workflow

```text
sklearn / xgboost / lightgbm / catboost  →  PMML  →  Pamplemousse  →  .lua
        (training)                       (export)    (--convert)    (deploy)
```

### 1. Train and export to PMML

The standard tool for sklearn-family models is
[`sklearn2pmml`](https://github.com/jpmml/sklearn2pmml). Wrap your
estimator in a `PMMLPipeline` and call `sklearn2pmml(...)`:

```python
from sklearn.ensemble import RandomForestClassifier
from sklearn2pmml import sklearn2pmml, PMMLPipeline

pipe = PMMLPipeline([("classifier", RandomForestClassifier(n_estimators=50))])
pipe.fit(X, y)
sklearn2pmml(pipe, "model.pmml", with_repr=True)
```

For boosting libraries:

| Library  | Wrap with                               |
| -------- | --------------------------------------- |
| XGBoost  | `XGBClassifier` / `XGBRegressor` directly inside `PMMLPipeline` |
| LightGBM | `LGBMClassifier` / `LGBMRegressor` directly inside `PMMLPipeline` |
| CatBoost | `CatBoostClassifier` / `CatBoostRegressor` directly inside `PMMLPipeline` |

### 2. Convert PMML to Lua

```bash
pamplemousse --convert -o model.lua model.pmml
```

The generated file defines a single `func(...)` that takes the model's
input fields as positional arguments and returns its outputs. Inspect
the first line of `model.lua` for the exact argument order.

### 3. Run it

```lua
dofile("model.lua")
local prob, label = func(0.42, 13.7, "AU", ...)
```

To validate against a CSV of expected outputs:

```bash
pamplemousse --test --data inputs.csv --verify expected.csv \
             --epsilon 0.0001 model.pmml
```

## Supported PMML model elements

Each row below is exercised by at least one unit test in `unit_tests/`.

| PMML element | Function | Test fixture | Test class |
| --- | --- | --- | --- |
| `TreeModel` | classification, regression | `TreeMissingValue.pmml`, `TreeNoTrueChild.pmml` | `TestTree` |
| `MiningModel` (ensembles) | classification, regression | `MiningModelMajority.pmml`, `MiningModelClassificationFirst.pmml`, `MiningModelRegressionAverage.pmml` | `TestMiningModel` |
| `NaiveBayesModel` | classification | `NaiveBayes.pmml` | `TestNaiveBayes` |
| `RuleSetModel` | classification | `RuleSetSimple.pmml`, `RuleSetComplex.pmml` | `TestRuleset` |
| `Scorecard` | regression + reason codes | `SampleScorcard.pmml`, `SampleScorecard-Attribute.pmml` | `TestScorecard` |
| `SupportVectorMachineModel` | classification | `SupportVectorBinary.pmml`, `SupportVectorXor.pmml` | `TestSupportVectorMachine` |

### MiningModel ensemble methods

`TestMiningModel` covers the following `multipleModelMethod` values:

- Classification: `majorityVote`, `weightedMajorityVote`, `average`,
  `weightedAverage`, `selectFirst` (weighted first), `max`.
- Regression: `average`, `weightedAverage`, `median`, `sum`, `max`.
- `modelChain` is exercised indirectly through the boosting libraries
  (each tree-ensemble export wraps a `MiningModel` chain).

CatBoost-style ensembles where every inner segment redeclares the
parent's `<OutputField>` are covered by
`testRegressionSumWithSegmentOutputs` (after the
`fix/catboost-output-shadowing` branch). This is the shape produced by
`sklearn2pmml`'s CatBoost exporter.

### Building blocks

These element families are exercised through transversal tests rather
than per-model fixtures:

| Area | Test class |
| --- | --- |
| `Predicate` (Simple, CompoundAnd/Or/Xor, Surrogate, SimpleSet) | `TestPredicate` |
| Built-in functions (`if`, comparison, string/numeric ops, missing handling) | `TestFunction` |
| Transformations (`Discretize`, `NormContinuous`, `NormDiscrete`, `MapValues`, `Constant`, `FieldRef`, dictionary import) | `TestTransform` |

## Confirmed-working boosting exports

The following exports from real Python pipelines have been validated
end-to-end against `joblib.predict` on synthetic inputs:

| Library | PMML root model | Status |
| --- | --- | --- |
| XGBoost (`XGBClassifier`) | `MiningModel` (modelChain → sum) | ✅ probabilities match `predict_proba` to ~1e-7 |
| LightGBM (`LGBMClassifier`) | `MiningModel` (modelChain → sum) | ✅ probabilities match to ~1e-15 |
| Random Forest (`sklearn`) | `MiningModel` (average) | ✅ exact match |
| CatBoost (`CatBoostClassifier`) | `MiningModel` (modelChain → sum) | ✅ raw `approx` matches to ~2e-10 (post fix) |

## Not yet covered by unit tests

The converter has source code for the following but no fixture-driven
tests in this branch. Use at your own risk and validate end-to-end:

- `NeuralNetwork`
- `RegressionModel` (linear / logistic, standalone)
- `GeneralRegressionModel`

If you adopt one of these and find it works, please add a unit test so
it joins the supported list.

## Worked example: minimal sklearn pipeline

```python
# train.py
from sklearn.datasets import load_iris
from sklearn.tree import DecisionTreeClassifier
from sklearn2pmml import sklearn2pmml, PMMLPipeline

X, y = load_iris(return_X_y=True, as_frame=True)
pipe = PMMLPipeline([("clf", DecisionTreeClassifier(max_depth=3))])
pipe.fit(X, y)
sklearn2pmml(pipe, "iris.pmml", with_repr=True)
```

```bash
$ pamplemousse --convert -o iris.lua iris.pmml
$ head -1 iris.lua
function func ( sepal_length, sepal_width, petal_length, petal_width )
$ lua -e 'dofile("iris.lua"); print(func(5.1, 3.5, 1.4, 0.2))'
0
```

`func`'s return signature follows the PMML model's `<Output>` block —
typically the predicted label first, then per-class probabilities.
