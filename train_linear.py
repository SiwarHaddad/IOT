import argparse, os, json, numpy as np, pandas as pd
from sklearn.linear_model import LinearRegression
from sklearn.metrics import mean_squared_error, r2_score
from sklearn.model_selection import train_test_split

ap = argparse.ArgumentParser()
ap.add_argument("--csv", required=True)
ap.add_argument("--label", default="label")
ap.add_argument("--outdir", default="model")
ap.add_argument("--test_size", type=float, default=0.2)
args = ap.parse_args()

df = pd.read_csv(args.csv)
features = ["temp","hum","alt","speed","density","hour"]
X = df[features].to_numpy(dtype=np.float64, copy=True)
y = df[args.label].to_numpy(dtype=np.float64, copy=True)

mask = np.isfinite(X).all(axis=1) & np.isfinite(y)
X = X[mask]; y = y[mask]

mean = X.mean(axis=0)
std = X.std(axis=0) + 1e-6
Xs = (X - mean)/std

Xtr, Xte, ytr, yte = train_test_split(Xs, y, test_size=args.test_size, random_state=42)

reg = LinearRegression()
reg.fit(Xtr, ytr)

pred = reg.predict(Xte)
rmse = np.sqrt(mean_squared_error(yte, pred))
r2 = r2_score(yte, pred)
print("RMSE:", rmse, "R2:", r2)

w = reg.coef_.ravel().tolist()
b = float(reg.intercept_)

os.makedirs(args.outdir, exist_ok=True)
with open(os.path.join(args.outdir, "lr_linear.json"), "w", encoding="utf-8") as f:
    json.dump({"w": w, "b": b}, f)
with open(os.path.join(args.outdir, "scaler.json"), "w", encoding="utf-8") as f:
    json.dump({"mean": mean.tolist(), "std": std.tolist()}, f)

print("saved", os.path.join(args.outdir,"lr_linear.json"))
print("saved", os.path.join(args.outdir,"scaler.json"))