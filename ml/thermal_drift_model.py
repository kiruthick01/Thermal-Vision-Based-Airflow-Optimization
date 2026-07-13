"""Lightweight NumPy MLP for temperature drift forecasting (PROJECT_PLAN.md section 6).

One hidden layer, manual forward pass + backprop, no autodiff / sklearn
dependency. Small enough to later port to raw C arrays / TFLite-Micro.
"""

import json

import numpy as np


class DriftMLP:
    """input -> Linear -> ReLU -> Linear -> scalar temperature prediction."""

    def __init__(self, input_dim, hidden_dim=16, seed=0):
        rng = np.random.default_rng(seed)
        self.input_dim = input_dim
        self.hidden_dim = hidden_dim
        self.W1 = rng.normal(0.0, np.sqrt(2.0 / input_dim), size=(input_dim, hidden_dim))
        self.b1 = np.zeros(hidden_dim)
        self.W2 = rng.normal(0.0, np.sqrt(2.0 / hidden_dim), size=(hidden_dim, 1))
        self.b2 = np.zeros(1)

    def forward(self, X):
        z1 = X @ self.W1 + self.b1
        a1 = np.maximum(z1, 0.0)
        z2 = a1 @ self.W2 + self.b2
        y = z2[:, 0]
        cache = (X, z1, a1)
        return y, cache

    def predict(self, X):
        y, _ = self.forward(X)
        return y

    def backward(self, cache, y_true, y_pred, lr):
        X, z1, a1 = cache
        n = X.shape[0]

        dz2 = (y_pred - y_true).reshape(-1, 1) * (2.0 / n)
        dW2 = a1.T @ dz2
        db2 = dz2.sum(axis=0)

        da1 = dz2 @ self.W2.T
        dz1 = da1 * (z1 > 0.0)
        dW1 = X.T @ dz1
        db1 = dz1.sum(axis=0)

        self.W2 -= lr * dW2
        self.b2 -= lr * db2
        self.W1 -= lr * dW1
        self.b1 -= lr * db1

    def train_step(self, X, y_true, lr):
        y_pred, cache = self.forward(X)
        loss = np.mean((y_pred - y_true) ** 2)
        self.backward(cache, y_true, y_pred, lr)
        return loss

    def param_count(self):
        return self.W1.size + self.b1.size + self.W2.size + self.b2.size

    def to_dict(self):
        return {
            "input_dim": self.input_dim,
            "hidden_dim": self.hidden_dim,
            "W1": self.W1.tolist(),
            "b1": self.b1.tolist(),
            "W2": self.W2.tolist(),
            "b2": self.b2.tolist(),
        }

    @classmethod
    def from_dict(cls, data):
        model = cls(data["input_dim"], data["hidden_dim"])
        model.W1 = np.array(data["W1"])
        model.b1 = np.array(data["b1"])
        model.W2 = np.array(data["W2"])
        model.b2 = np.array(data["b2"])
        return model

    def save_weights(self, path):
        with open(path, "w") as f:
            json.dump(self.to_dict(), f)

    @classmethod
    def load_weights(cls, path):
        with open(path) as f:
            data = json.load(f)
        return cls.from_dict(data)
