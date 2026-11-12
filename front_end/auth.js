const username_symbol = document.getElementById("username");
const password_input = document.getElementById("password");
const button_login = document.getElementById("btn-login");
const button_register = document.getElementById("btn-register");
const authMessage = document.getElementById("auth-message");

function getToken() {
    return localStorage.getItem('jwt_token');
}

function clearToken() {
    localStorage.removeItem('jwt_token');
}

async function authFetch(url, options = {}) {
    const token = getToken();
    const headers = { ...(options.headers || {}) };
    if (token) headers['Authorization'] = 'Bearer ' + token;
    return fetch(url, { ...options, headers });
}

async function checkAuth() {
    const token = getToken();
    if (!token) {
        window.location.href = '/login.html';
        return false;
    }
    try {
        const res = await authFetch('/api/tokens');
        if (!res.ok) {
            clearToken();
            window.location.href = '/login.html';
            return false;
        }
        const data = await res.json();
        console.log('Авторизован как', data.username);
        return true;
    } catch (e) {
        console.error(e);
        clearToken();
        window.location.href = '/login.html';
        return false;
    }
}

async function logout() {
    try {
        await authFetch('/api/logout', { method: 'POST' });
    } catch (e) {}
    clearToken();
    window.location.href = '/login.html';
}

async function sendAuthRequest(event, url, success_message) {
    event.preventDefault();

    const username = username_symbol.value;
    const password = password_input.value;

    if (!username || !password) {
        authMessage.innerText = "Заполните все поля";
        authMessage.style.color = "red";
        authMessage.style.display = "block";
        return;
    }

    const data = { username: username, password: password };

    button_login.disabled = true;
    button_register.disabled = true;
    authMessage.style.display = "none";

    try {
        let response = await fetch(url, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json;charset=utf-8' },
            body: JSON.stringify(data)
        });

        let result;
        try {
            result = await response.json();
        } catch (e) {
            throw new Error("Некорректный ответ от сервера");
        }

        if (!response.ok) {
            authMessage.innerText = "Ошибка: " + (result.error || "Неизвестная ошибка");
            authMessage.style.color = "red";
            authMessage.style.display = "block";
            return;
        }

        if (url.includes("login")) {
            localStorage.setItem('jwt_token', result.token);
            window.location.href = "/index.html";
            return;
        }

        authMessage.innerText = success_message;
        authMessage.style.color = "var(--success-color, #2ec4b6)";
        authMessage.style.display = "block";

        username_symbol.value = "";
        password_input.value = "";

    } catch (error) {
        console.error(error);
        authMessage.innerText = error.message || "Не удалось связаться с сервером";
        authMessage.style.color = "red";
        authMessage.style.display = "block";
    } finally {
        button_login.disabled = false;
        button_register.disabled = false;
    }
}

if (button_login) {
    button_login.addEventListener('click', (e) => sendAuthRequest(e, '/api/login', 'Успешно авторизированы'));
}

if (button_register) {
    button_register.addEventListener('click', (e) => sendAuthRequest(e, '/api/register', 'Пользователь успешно зарегистрирован! Теперь войдите'));
}

const button_logout = document.getElementById("btn-logout");
if (button_logout) {
    button_logout.addEventListener('click', logout);
}