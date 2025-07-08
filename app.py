from flask import Flask, request, jsonify
import requests
import os
from ultralytics import YOLO
import cv2
from PIL import Image
from io import BytesIO
from twilio.rest import Client
from google.cloud import storage
import uuid
import numpy as np


app = Flask(__name__)


# Load the YOLOv8 model
model = YOLO('best.pt')  # Replace with your trained model if needed


# Twilio credentials
TWILIO_ACCOUNT_SID = ' '
TWILIO_AUTH_TOKEN = ' '
TWILIO_PHONE_NUMBER = ' '
DESTINATION_PHONE_NUMBER = ' '


# GCP Storage settings
BUCKET_NAME = ' '  # Replace with your bucket name
ORIGINAL_FOLDER = 'upload/'
ANNOTATED_FOLDER = 'annotated/'


# Initialize Twilio client
client = Client(TWILIO_ACCOUNT_SID, TWILIO_AUTH_TOKEN)


# Initialize Google Cloud Storage client
storage_client = storage.Client()


from werkzeug.utils import secure_filename


@app.route('/upload_image', methods=['POST'])
def upload_image():
   if 'file' not in request.files or request.files['file'].filename == '':
       # Gracefully handle missing image, but allow detection system to continue
       print("No image uploaded. Proceeding without upload.")
       return jsonify({"message": "No image uploaded, continuing with previous or existing image."}), 200


   file = request.files['file']


   try:
       bucket = storage_client.bucket(BUCKET_NAME)
       filename = secure_filename(file.filename)
       blob_name = ORIGINAL_FOLDER + 'image_' + str(uuid.uuid4()) + '_' + filename
       blob = bucket.blob(blob_name)


       # Save file to Cloud Storage
       blob.upload_from_file(file, content_type='image/jpeg')


       print(f"Image uploaded successfully to: {blob_name}")
       return jsonify({"message": "Image uploaded successfully", "path": blob_name}), 200


   except Exception as e:
       print(f"Error uploading image: {e}")
       return jsonify({"error": str(e)}), 500




def shorten_url(url):
   try:
       api_url = f"http://tinyurl.com/api-create.php?url={url}"
       response = requests.get(api_url)
       if response.status_code == 200:
           return response.text
       else:
           return url  # fallback to original if error
   except Exception as e:
       print(f"Error shortening URL: {e}")
       return url
      
def get_latest_image():
   try:
       bucket = storage_client.get_bucket(BUCKET_NAME)
       blobs = list(bucket.list_blobs(prefix=ORIGINAL_FOLDER))
       blobs = [b for b in blobs if not b.name.endswith('/')]  # Ignore folders


       if not blobs:
           print("No images found in the upload folder.")
           return None, None, None


       latest_blob = max(blobs, key=lambda b: b.updated)
       image_url = f"https://storage.googleapis.com/{BUCKET_NAME}/{latest_blob.name}"


       img_data = latest_blob.download_as_bytes()
       img = Image.open(BytesIO(img_data)).convert('RGB')


       print(f"Latest original image found: {latest_blob.name}")
       return img, latest_blob.name, image_url


   except Exception as e:
       print(f"Error fetching image: {e}")
       return None, None, None


def upload_annotated_image(img_array):
   try:
       bucket = storage_client.get_bucket(BUCKET_NAME)
       annotated_blob_name = ANNOTATED_FOLDER + 'annotated_' + str(uuid.uuid4()) + '.jpg'


       # Directly encode the BGR image as JPEG
       _, buffer = cv2.imencode('.jpg', img_array)


       # Upload to GCS
       blob = bucket.blob(annotated_blob_name)
       blob.upload_from_string(buffer.tobytes(), content_type='image/jpeg')


       annotated_url = f"https://storage.googleapis.com/{BUCKET_NAME}/{annotated_blob_name}"
       print(f"Uploaded annotated image: {annotated_url}")


       return annotated_url


   except Exception as e:
       print(f"Error uploading annotated image: {e}")
       return None


   except Exception as e:
       print(f"Error uploading annotated image: {e}")
       return None


def send_sms(message_body):
   try:
       message = client.messages.create(
           body=message_body,
           from_=TWILIO_PHONE_NUMBER,
           to=DESTINATION_PHONE_NUMBER
       )
       print(f"SMS sent successfully: SID {message.sid}")
   except Exception as e:
       print(f"Failed to send SMS: {e}")


@app.route('/', methods=['GET'])
def home():
   return "Hello, Fire Detection Service is Active!"


@app.route('/', methods=['POST'])
def detect_fire():
   data = request.get_json()


   if not data or not all(k in data for k in ("temperature", "humidity", "gas_level","latitude","longitude")):
       return jsonify({"error": "Missing sensor data!"}), 400


   try:
       # Get sensor readings
       temperature = data['temperature']
       humidity = data['humidity']
       gas_level = data['gas_level']
       latitude = data['latitude']
       longitude = data['longitude']


       print(f"Received Sensor Data - Temp: {temperature}°C, Humidity: {humidity}%, Gas: {gas_level}, Latitude: {latitude}, Longitude: {longitude}")


       # Get latest image from original folder
       img, original_image_name, original_image_url = get_latest_image()
       if img is None:
           return jsonify({"error": "No image found for processing."}), 500


       # Run YOLOv8 model
       results = model.predict(img, conf=0.5, save=False)


       # Analyze detection
       fire_detected = any(
           int(box.cls[0]) == 0 for result in results for box in result.boxes
       )


       detection_result = "FIRE DETECTED" if fire_detected else "No Fire"
       print(f"Detection Result: {detection_result}")


       # Save annotated image (result is numpy array)
       annotated_img = results[0].plot()  # Get annotated BGR image (numpy array)
       annotated_url = upload_annotated_image(annotated_img)


       if latitude and longitude:
           location_link = f"https://www.google.com/maps?q={latitude},{longitude}"
           location_link = shorten_url(location_link)
       else:
           location_link = "Unavailable"


       annotated_url = shorten_url(annotated_url)
      
       # Send shortened SMS
       sms_body = (
           f"🔥 Alert: {detection_result}\n"
           f"Temperature:{temperature}°C\n"
           f"Humidity:{humidity}%\n"
           f"Gas:{gas_level}ppm\n"
           f"Loc: {location_link}\n"
           f"Img: {annotated_url}"
       )
       send_sms(sms_body)
       return jsonify({
           "detection_result": detection_result,
           "message": "Image processed and SMS sent successfully!"
       })


   except Exception as e:
       print(f"Error processing request: {e}")
       return jsonify({"error": str(e)}), 500


if __name__ == '__main__':
   port = int(os.environ.get("PORT", 8080))
   app.run(host='0.0.0.0', port=port)


