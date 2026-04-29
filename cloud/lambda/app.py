"""
SenseGate — Flask wrapper per Railway
Espone lo stesso handler Lambda come endpoint HTTP POST /webhook

Railway si aspetta che l'app ascolti su PORT (env var).
Twilio manda POST a https://<app>.railway.app/webhook
"""

import os
from flask import Flask, request, Response
from handler import handler as lambda_handler

app = Flask(__name__)


@app.route("/webhook", methods=["POST"])
def webhook():
    # Costruisce un evento compatibile con il formato Lambda
    # che handler.py si aspetta
    event = {
        "headers": dict(request.headers),
        "body": request.get_data(as_text=True),
        "isBase64Encoded": False,
        "rawPath": "/webhook",
        "path": "/webhook",
    }

    result = lambda_handler(event, context=None)

    return Response(
        result.get("body", ""),
        status=result.get("statusCode", 200),
        mimetype=result.get("headers", {}).get("Content-Type", "text/xml"),
    )


@app.route("/health", methods=["GET"])
def health():
    return {"status": "ok"}, 200


if __name__ == "__main__":
    port = int(os.environ.get("PORT", 8080))
    app.run(host="0.0.0.0", port=port)