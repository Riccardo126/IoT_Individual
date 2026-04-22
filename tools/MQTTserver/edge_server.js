const mqtt = require('mqtt');

// Connettiti al broker locale (sul tuo stesso PC)
const client = mqtt.connect('mqtt://localhost'); // Usa localhost

client.on('connect', () => {
    console.log('✅ Edge Server connesso al Broker MQTT locale');
    client.subscribe('iot/average'); // Ascolta i dati dall'ESP32
});

client.on('message', (topic, message) => {
    if (topic === 'iot/average') {
        const payload = message.toString();
        console.log(`[${new Date().toISOString()}] Ricevuto dato: ${payload}`);
        
        // RISPOSTA ISTANTANEA (PING-PONG)
        // Mandiamo un segnale di "ricevuto" sull'altro topic
        client.publish('iot/ack', 'ACK');
        console.log(` ↳ Inviato ACK di conferma all'ESP32`);
    }
});