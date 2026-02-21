from flask import Flask, render_template, jsonify, request
import time
import math
from datetime import datetime
import csv
import sqlite3
import os
import json
now = datetime.now()



# Server settings
host = "0.0.0.0"  # Listen on all available network interfaces
port = 5000       # Match the port forwarded in the router


db_folder = "databases"

updates={}


app = Flask(__name__)

def init_db(db_path):
    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()
    cursor.execute("""
    CREATE TABLE IF NOT EXISTS sensor_data (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        timestamp TEXT NOT NULL,
        lux REAL NOT NULL,
        sound REAL NOT NULL
    )
    """)
    conn.commit()
    conn.close()


@app.route('/time', methods=['GET','POST'])
def get_time():
    print("sending time")
    current_epoch = int(time.time())  # current epoch in seconds
    return jsonify({"time": current_epoch})


pocet=0
@app.route('/receive_data', methods=['GET','POST'])
def receive_data():
    global pocet
    try:
        print("reciving data")
        print(pocet)
        pocet+=1
        # Get JSON from request
        data = request.get_json()
        node_name = data.get("node_name")
        voltage = data.get("voltage")
        battery = data.get("battery")
        timestamp_list = data.get("timestamp")
        sound_list = data.get("sound")
        lux_list = data.get("lux")
        print("voltage: ", voltage, "percentage: ", battery)
        # Check required fields
        if voltage is None or battery is None or sound_list is None or lux_list is None or timestamp_list is None or node_name is None:
            return jsonify({"status": "error", "message": "Missing required fields"}), 400
        # Database insert
        db_path = os.path.join(db_folder, f"{node_name}.db")
        init_db(db_path)  # ensures DB and table exist

        conn = sqlite3.connect(db_path)
        cursor = conn.cursor()

        for ts, lux, sound in zip(timestamp_list, lux_list, sound_list):
            ts+=1760000000
            cursor.execute(
                "INSERT INTO sensor_data (timestamp, lux, sound) VALUES (?, ?, ?)",
                (ts, lux, sound)
            )

        conn.commit()
        conn.close()
        print(f"Inserted {len(timestamp_list)} rows for {node_name}")

        # Always return a response
        return jsonify({"status": "success", "inserted_rows": len(timestamp_list)}), 200

    except Exception as e:
        print("Error:", e)
        return jsonify({"status": "error", "message": str(e)}), 400


def get_data(marker_id, date=None):
    if date is None:
        date = datetime.today().strftime('%Y-%m-%d')

    db_path = os.path.join(db_folder, f"{marker_id}.db")

    if not os.path.exists(db_path):
        print(f"Database for {marker_id} does not exist.")
        return None

    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()

    # Keep DATE(timestamp, 'unixepoch') so we can filter by date
    cursor.execute("""
        SELECT timestamp, lux, sound FROM sensor_data
        WHERE DATE(timestamp, 'unixepoch') = ?
        ORDER BY timestamp ASC
    """, (date,))

    rows = cursor.fetchall()
    conn.close()

    if not rows:
        print(f"No data found for {marker_id} on {date}")
        return {
        "current_light": None,
        "current_loudness": None,
        "timestamps": None,
        "light_intensity": -255,
        "loudness": -255,
        }

    list_timestamps = []
    list_light_intensity = []
    list_loudness = []

    data = None

    for ts, lux, sound in rows:
        list_timestamps.append(int(ts))   # ? keep raw epoch
        list_light_intensity.append(lux)
        list_loudness.append(sound)
        data = (ts, lux, sound)
    if marker_id in updates:
        return {
            "current_light": data[1],
            "current_loudness": data[2],
            "timestamps": list_timestamps,
            "light_intensity": list_light_intensity,
            "loudness": list_loudness,
            "Battery Percentage": updates[marker_id][0],
            "Voltage": updates[marker_id][1],
            "Last Update": updates[marker_id][2]
            }
    else:
        return{
        "current_light": data[1],
        "current_loudness": data[2],
        "timestamps": list_timestamps,
        "light_intensity": list_light_intensity,
        "loudness": list_loudness
        }

markers = [
    {"id": "durdosik", "name": "Durdosik", "lat": 48.7421193, "lon": 21.4153122},
    {"id": "sobrance", "name": "Sobrance", "lat": 48.749286, "lon": 22.181093},
    {"id": "bratislava", "name": "Bratislava", "lat": 48.150034, "lon": 17.065442},
    {"id": "kosice", "name": "Kosice", "lat": 48.728882 , "lon": 21.248280},
    {"id": "kosice_2", "name": "Kosice_sever", "lat": 48.738852 , "lon": 21.245107},
    {"id": "test", "name": "test", "lat": 48.7421100 , "lon": 21.4153100}

]
@app.route('/')
def index():
    print("index")
    return render_template('index.html', markers=markers)

@app.route('/data/<marker_id>')
def marker_data(marker_id):
    print("marker_data")
    return jsonify(get_data(marker_id,request.args.get('date')))

@app.route('/graph/<marker_id>')
def graph_page(marker_id):
    print("graph_page")
    return render_template('graph.html', marker_id=marker_id)

@app.route('/historical/<marker_id>')
def historical_data(marker_id):
    print(request.args.get('date'))
    print("historical_data")
    print(marker_id)
    return jsonify(get_data(marker_id,request.args.get('date')))
if __name__ == '__main__':
    from waitress import serve
    serve(app,host='0.0.0.0', port=5000)