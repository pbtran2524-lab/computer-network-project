const express = require('express');
const http = require('http');
const { Server } = require('socket.io');

const app = express();
const server = http.createServer(app);
const io = new Server(server, {
    cors: { origin: "*" },
    maxHttpBufferSize: 1e8 // Cho phép gửi file/ảnh lên tới 100MB
});

// Map để lưu trữ Timeout
const consentTimeouts = new Map(); 

io.on('connection', (socket) => {
    const { type, id } = socket.handshake.query; 
    console.log(`[+] ${type} kết nối: ${id}`);

    // Khi Agent vừa online, tự động tạo 1 room mang tên chính ID của nó
    if (type === 'agent') {
        socket.join(id); 
        io.emit('agent_status', { agent_id: id, status: 'online' }); // Báo cho mọi Web biết
    }

    // Khi Web muốn điều khiển Agent nào, cho Web join vào room của Agent đó
    socket.on('web_join_agent_room', (data) => {
        socket.join(data.agent_id);
        console.log(`Web đã join vào room của Agent: ${data.agent_id}`);
    });

    socket.on('disconnect', () => {
        console.log(`[-] ${type} ngắt kết nối: ${id}`);
        if (type === 'agent') {
            io.emit('agent_status', { agent_id: id, status: 'offline' });
        }
    });

    // ==========================================
    // LOGIC TIMEOUT XIN QUYỀN (Consent Flow)
    // ==========================================
    socket.on('send_command_to_agent', (data) => {
        // Gửi lệnh vào room của Agent đó (chỉ Agent trong room mới nhận được)
        io.to(data.agent_id).emit('execute_command', data);

        if (['live_screen', 'webcam', 'keylogger'].includes(data.module)) {
            const timeoutId = setTimeout(() => {
                io.to(data.agent_id).emit('consent_response', { 
                    agent_id: data.agent_id, 
                    module: data.module,
                    status: 'timeout' 
                });
                consentTimeouts.delete(data.agent_id);
            }, 30000); 

            consentTimeouts.set(data.agent_id, timeoutId);
        }
    });

    socket.on('consent_response_from_agent', (data) => {
        if (data.status === 'accepted' || data.status === 'rejected') {
            const timeoutId = consentTimeouts.get(data.agent_id);
            if (timeoutId) {
                clearTimeout(timeoutId);
                consentTimeouts.delete(data.agent_id);
            }
        }
        // Trả kết quả về cho Web trong cùng room
        io.to(data.agent_id).emit('consent_response', data);
    });

    // ==========================================
    // LUỒNG MEDIA (Live Screen / Webcam)
    // ==========================================
    socket.on('media_stream_from_agent', (data) => {
        // Agent quăng binary data vào room, Web nằm sẵn trong đó sẽ tự động nhận được
        socket.to(data.agent_id).emit('media_stream_to_web', data);
    });
});

server.listen(3000, () => {
    console.log('Central Server (Broker) đang chạy tại Port 3000');
});