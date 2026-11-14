let button = document.getElementById("btn");
let select = document.getElementById("route-select");
let input_date = document.getElementById("date-input");
let input_price = document.getElementById("price-input");

let result_block = document.getElementById("result-block");
let res_seats = document.getElementById("res-seats");
let res_will_buy = document.getElementById("res-will-buy");
let btn_logout = document.getElementById('btn-logout');
let btn_coffee = document.getElementById('btn-coffee');

let form = document.getElementById("forecast-form");

btn_coffee.addEventListener('click', async () => {
    let response = await fetch('/make_coffee' , {
        method: 'GET',
        headers: {
            'Content-Type': 'text/plain',
        },
    });
    let answer = await response.text();
    console.log(answer);
    alert("error 418!!! " + answer);
});

button.addEventListener('click', async (event) => {
    event.preventDefault();

    const route = select.value;
    const date = input_date.value;
    const price = input_price.value;

    if(!route || !date || !price) {
        alert("Заполните все поля формы");
        return;
    }

    const data = { route: route, date: date, price: price };

    button.disabled = true;
    button.innerText = "Расчет";
    result_block.style.display = 'none';

    const token = localStorage.getItem('jwt_token');

    try {
        let response = await fetch('/api/count', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json;charset=utf-8',
                'Authorization': 'Bearer ' + token
            },
            body: JSON.stringify(data)
        });

        let result;
        try {
            result = await response.json();
        } catch (e) {
            throw new Error("Ошибка формата ответа сервера");
        }

        if (!response.ok) {
            throw new Error(result.error || "Ошибка при расчете");
        }

        res_seats.innerText = result.seats;
        res_will_buy.innerText = result.will_buy;
        result_block.style.display = 'block';

    } catch (error) {
        console.error(error);
        alert(error.message);
    } finally {
        button.disabled = false;
        button.innerText = "Рассчитать спрос";
    }
});

btn_logout.addEventListener('click', async () => {
    const token = localStorage.getItem('jwt_token');
    try {
        if (token) {
            await fetch('/api/logout', {
                method: 'POST',
                headers: { 'Authorization': 'Bearer ' + token }
            });
        }
    } catch (e) {
        console.error('logout error', e);
    } finally {
        localStorage.removeItem('jwt_token');
        window.location.href = '/login.html';
    }
});

if(form) {
    form.addEventListener('submit', function(event) {
        event.preventDefault();
        const btn = document.getElementById("btn");
        if(btn) btn.click();
    });
}