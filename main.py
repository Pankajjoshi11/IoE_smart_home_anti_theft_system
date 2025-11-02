from flask import Flask, request, jsonify
import requests
import logging

# =================== CONFIGURATION ===================
# Get these from Telegram's BotFather
TELEGRAM_BOT_TOKEN = '8361278888:AAFYifonJowcNItq_MiCXqaIhuyPL1uBa2s'

# Get this by messaging your bot and visiting: https://api.telegram.org/bot<YOUR_TOKEN>/getUpdates
TELEGRAM_CHAT_ID = '7878423855' 
# ===================================================

# Configure logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')

app = Flask(__name__)

def send_telegram_message(message):
    """Sends a message to the configured Telegram chat."""
    if not TELEGRAM_BOT_TOKEN or not TELEGRAM_CHAT_ID:
        logging.error("Telegram BOT_TOKEN or CHAT_ID is not set.")
        return False
    
    api_url = f"https://api.telegram.org/bot{TELEGRAM_BOT_TOKEN}/sendMessage"
    
    payload = {
        'chat_id': TELEGRAM_CHAT_ID,
        'text': message,
        'parse_mode': 'Markdown'
    }
    
    try:
        response = requests.post(api_url, json=payload, timeout=10)
        response.raise_for_status() # Raises an exception for bad status codes (4xx or 5xx)
        logging.info(f"Successfully sent message to Telegram: {message}")
        return True
    except requests.exceptions.RequestException as e:
        logging.error(f"Failed to send message to Telegram: {e}")
        return False

@app.route('/alert', methods=['POST'])
def handle_alert():
    """Receives alerts from the ESP8266 and forwards them."""
    if not request.is_json:
        logging.warning("Received a non-JSON request.")
        return jsonify({"status": "error", "message": "Invalid content type, expected application/json"}), 400

    data = request.get_json()
    message = data.get('message')

    if not message:
        logging.warning("Received a request with no message.")
        return jsonify({"status": "error", "message": "Missing 'message' field in JSON payload"}), 400

    # Log the received message
    logging.info(f"🚨 Received alert from NodeMCU: '{message}'")
    
    # Forward the message to Telegram
    if send_telegram_message(message):
        return jsonify({"status": "success", "message": "Alert received and forwarded to Telegram."}), 200
    else:
        return jsonify({"status": "error", "message": "Alert received but failed to forward to Telegram."}), 500

if __name__ == '__main__':
    # Run the app on all available network interfaces
    # This makes it accessible from other devices on the same network
    app.run(host='0.0.0.0', port=5000, debug=True)