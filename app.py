
import streamlit as st
import numpy as np
import pandas as pd
import plotly.express as px
from sklearn.ensemble import IsolationForest

# -------------------------------
# 1. DASHBOARD CONFIGURATION
# -------------------------------
st.set_page_config(
    page_title="EV Motor Digital Twin",
    page_icon="⚡",
    layout="wide"
)

st.title("EV Motor Digital Twin")
st.caption(
    "STM32 + Python + AI/ML | Prototype using simulated motor data"
)

# -------------------------------
# 2. MOTOR SIMULATOR
# -------------------------------
def simulate_motor(samples=200, scenario="Normal", seed=42):
    rng = np.random.default_rng(seed)
    time = np.arange(samples)

    # Simulated operating conditions
    speed = 1200 + 200 * np.sin(time / 15)
    load = 0.5 + 0.1 * np.sin(time / 20)

    # Simplified virtual motor model
    expected_temp = 25 + 0.015 * speed + 12 * load
    expected_current = 0.8 + 0.003 * speed + 5 * load

    temperature = expected_temp + rng.normal(0, 0.8, samples)
    current = expected_current + rng.normal(0, 0.15, samples)

    # Known labels for testing injected events
    event = np.zeros(samples, dtype=int)
    start = samples // 2

   
    if scenario == "Overheating":
        # Temperature rises gradually; current remains normal.
        temperature[start:] += np.linspace(
            0.5, 15, samples - start
        )
        event[start:] = 1

    elif scenario == "High current":
        # Current increases; temperature follows its normal model.
        current[start:] += np.linspace(
    0.2, 3.0, samples - start
)
        event[start:] = 1

    elif scenario == "Combined fault":
        # Both temperature and current become abnormal.
        temperature[start:] += np.linspace(
            0.5, 15, samples - start
        )
        current[start:] += np.linspace(
         0.2, 3.0, samples - start )
        event[start:] = 1


    # Model residuals: observed minus expected
    temp_residual = temperature - expected_temp
    current_residual = current - expected_current

    return pd.DataFrame({
        "Time (s)": time,
        "Speed (RPM)": speed,
        "Load": load,
        "Expected temperature (C)": expected_temp,
        "Temperature (C)": temperature,
        "Expected current (A)": expected_current,
        "Current (A)": current,
        "Temperature residual": temp_residual,
        "Current residual": current_residual,
        "Injected event": event
    })


# -------------------------------
# 3. TRAIN AI/ML ON NORMAL DATA
# -------------------------------
training_data = simulate_motor(
    samples=3000,
    scenario="Normal",
    seed=100
)

features = [
    "Temperature residual",
    "Current residual"
]

detector = IsolationForest(
    n_estimators=100,
    contamination=0.03,
    random_state=42
)

detector.fit(training_data[features])

# -------------------------------
# 4. DASHBOARD CONTROLS
# -------------------------------
st.sidebar.header("Simulation Controls")

scenario = st.sidebar.selectbox(
    "Motor operating scenario",
    [
        "Normal",
        "Overheating",
        "High current",
        "Combined fault"
    ]
)

samples = st.sidebar.slider(
    "Number of samples",
    min_value=100,
    max_value=500,
    value=200,
    step=50
)

# Generate the selected test run
data = simulate_motor(
    samples=samples,
    scenario=scenario,
    seed=2026
)

# AI prediction: -1 = anomaly, +1 = normal
data["AI prediction"] = detector.predict(data[features])
data["AI anomaly"] = (
    data["AI prediction"] == -1
).astype(int)

# -------------------------------
# 5. HEALTH / STATUS INDICATORS
# -------------------------------
latest = data.iloc[-1]
anomaly_count = int(data["AI anomaly"].sum())

c1, c2, c3, c4 = st.columns(4)

c1.metric(
    "Motor temperature",
    f"{latest['Temperature (C)']:.1f} °C"
)
c2.metric(
    "Motor current",
    f"{latest['Current (A)']:.2f} A"
)
c3.metric(
    "Speed",
    f"{latest['Speed (RPM)']:.0f} RPM"
)
c4.metric(
    "AI anomaly samples",
    anomaly_count
)

if scenario == "Normal":
    st.info(
        "Normal test selected. Any AI alerts should be checked "
        "as potential false alarms."
    )
else:
    st.warning(
        f"Simulated {scenario} selected. "
        "This is a test scenario, not a real motor fault."
    )

# -------------------------------
# 6. DIGITAL TWIN GRAPHS
# -------------------------------
st.subheader("Digital Twin: Temperature")

fig_temp = px.line(
    data,
    x="Time (s)",
    y=[
        "Expected temperature (C)",
        "Temperature (C)"
    ],
    labels={
        "value": "Temperature (C)",
        "variable": "Signal"
    }
)
st.plotly_chart(fig_temp, use_container_width=True)

st.subheader("Digital Twin: Current")

fig_current = px.line(
    data,
    x="Time (s)",
    y=[
        "Expected current (A)",
        "Current (A)"
    ],
    labels={
        "value": "Current (A)",
        "variable": "Signal"
    }
)
st.plotly_chart(fig_current, use_container_width=True)

# -------------------------------
# 7. MODEL RESIDUALS
# -------------------------------
st.subheader("Model Residual Monitoring")

fig_residual = px.line(
    data,
    x="Time (s)",
    y=[
        "Temperature residual",
        "Current residual"
    ],
    labels={
        "value": "Residual",
        "variable": "Signal"
    }
)
st.plotly_chart(fig_residual, use_container_width=True)

# -------------------------------
# 8. AI ANOMALY VISUALIZATION
# -------------------------------
st.subheader("AI/ML Anomaly Detection")

data["AI status"] = data["AI anomaly"].map({
    0: "Normal",
    1: "Anomaly"
})

fig_ai = px.scatter(
    data,
    x="Time (s)",
    y="Temperature (C)",
    color="AI status",
    hover_data=[
        "Current (A)",
        "Temperature residual",
        "Current residual"
    ],
    title="AI classification of simulated motor readings"
)

st.plotly_chart(fig_ai, use_container_width=True)

# -------------------------------
# 9. VALIDATION METRICS
# -------------------------------
st.subheader("Test Run Evaluation")

actual = data["Injected event"].to_numpy()
predicted = data["AI anomaly"].to_numpy()

tp = int(((actual == 1) & (predicted == 1)).sum())
fp = int(((actual == 0) & (predicted == 1)).sum())
fn = int(((actual == 1) & (predicted == 0)).sum())
tn = int(((actual == 0) & (predicted == 0)).sum())

normal_count = int((actual == 0).sum())
event_count = int((actual == 1).sum())

false_alarm_rate = (
    fp / normal_count * 100 if normal_count else 0
)
miss_rate = (
    fn / event_count * 100 if event_count else None
)
    
# Detection delay after an injected event begins
detection_delay = None

if event_count > 0:
    event_start = int(np.flatnonzero(actual == 1)[0])

    detections_after_start = np.flatnonzero(
        predicted[event_start:] == 1
    )

    if len(detections_after_start) > 0:
        # One sample represents one simulated second
        detection_delay = int(detections_after_start[0])





m1, m2, m3, m4 = st.columns(4)

m1.metric("False alarm samples", fp)
m2.metric("Missed event samples", fn)
m3.metric("Detected event samples", tp)

delay_text = (
    f"{detection_delay} s"
    if detection_delay is not None
    else "Not detected" if event_count else "N/A"
)

m4.metric("Detection delay", delay_text)

st.write(f"False alarm rate: {false_alarm_rate:.1f}%")

if miss_rate is not None:
    st.write(f"Miss rate: {miss_rate:.1f}%")
else:
    st.write("Miss rate: Not applicable; no event was injected.")

st.caption(
    "Metrics are from this synthetic test run. "
    "Detection delay is measured from the start of an injected event "
    "to the first subsequent AI anomaly flag. It is not a real motor "
    "measurement or a guarantee of physical fault detection."
)


# -------------------------------
# 10. TELEMETRY TABLE + DOWNLOAD
# -------------------------------
with st.expander("View simulated telemetry"):
    st.dataframe(data, use_container_width=True)

st.download_button(
    "Download test data as CSV",
    data=data.to_csv(index=False).encode("utf-8"),
    file_name="motor_test_data.csv",
    mime="text/csv"
)
