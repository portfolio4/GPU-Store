// State Management
let g_current_product_id;
let g_current_products;
let g_websocket;
let g_user_orders = [];
let g_user_trades = []
let g_last_market_data_snapshot = {}


async function fetchJsonObject(path) {
    let response;
    try {
        response = await(fetch("products/products.json"));
        if (!response.ok) {
            console.error("Fetch product request failed: {}", response.status);
            return;
        }
    } catch (error) {
        console.error("Could not fetch products: {}", error);
        return;
    }
    return await response.json();
}

async function loadSymbolsInUI() {
    g_current_products = await fetchJsonObject("products/products.json");

    const productsList = document.getElementById('product-list');
    for (const [key, product] of Object.entries(g_current_products)) {
        productsList.innerHTML += `
                <li class="clickable" onclick="switchProduct(this)" data-product-id="${product.id}">
                    <span>${product.ticker}</span>
                </li>`;
    }

    const first_product_elem = productsList.firstElementChild;
    switchProduct(first_product_elem);
}

function switchProduct(element) {
    const product_id = element.getAttribute("data-product-id");
    const product_data = g_current_products[product_id];

    g_current_product_id = product_data.id;
    document.getElementById('current-product').innerText = product_data.ticker;
    document.getElementById('current-product-summary').innerText = product_data.summary;
    document.getElementById('current-product-image').src = `products/${g_current_product_id}/image`;

    const allProducts = document.querySelectorAll('#product-list li');
    allProducts.forEach(li => li.classList.remove('active'));
    if (element) {
        element.classList.add('active');
    }

    updateOrderBook();
}

function updateOrderBook() {
    const asksList = document.getElementById('asks-list');
    const bidsList = document.getElementById('bids-list');
    const spread = document.getElementById('spread-val');

    asksList.innerHTML = '';
    bidsList.innerHTML = '';

    let bids;
    let asks;
    let price_divisor;
    if (g_current_product_id in g_last_market_data_snapshot) {
        asks = g_last_market_data_snapshot[g_current_product_id].asks;
        bids = g_last_market_data_snapshot[g_current_product_id].bids;
        price_divisor = g_current_products[g_current_product_id].price_divisor;
    } else {
        asks = [];
        bids = [];
        price_divisor = 1;
    }

    const BOOK_DEPTH = 5;

    for (let i = BOOK_DEPTH - 1; i >= 0; --i) {
        if (i >= asks.length || asks[i].qty == 0) {
            asksList.innerHTML += `
                <div class="grid-row">
                    <span>&nbsp;</span>
                    <span>&nbsp;</span>
                </div>`;
        } else {
            let askPrice = asks[i].price / price_divisor;
            let askSize = asks[i].qty;
            asksList.innerHTML += `
                <div class="grid-row clickable" onclick="setPrice(${askPrice})">
                    <span>$${askPrice}</span>
                    <span>${askSize}</span>
                </div>`;
        }
    }
    for (let i = 0; i < BOOK_DEPTH; ++i) {
        if (i >= bids.length || bids[i].qty == 0) {
            bidsList.innerHTML += `
                <div class="grid-row">
                    <span>&nbsp;</span>
                    <span>&nbsp;</span>
                </div>`;
        } else {
            let bidPrice = bids[i].price / price_divisor;
            let bidSize = bids[i].qty;
            bidsList.innerHTML += `
                <div class="grid-row clickable" onclick="setPrice(${bidPrice})">
                    <span>$${bidPrice}</span>
                    <span>${bidSize}</span>
                </div>`;
        }
    }

    if (bids.length > 0 && bids[0].price != 0 && asks.length > 0 && asks[0].price != 0) {
        spread.innerHTML = `${(asks[0].price - bids[0].price) / price_divisor}`;
    } else {
        spread.innerHTML = `N/A`;
    }
}

// Helper to fill the order form when clicking a price
function setPrice(price) {
    document.getElementById('order-price').value = price;
}

function submitOrder(side) {
    const priceInput = document.getElementById('order-price');
    const qtyInput = document.getElementById('order-qty');
    const dayOption = document.getElementById("order-duration-day");
    const iocOption = document.getElementById("order-duration-ioc");

    if(!priceInput.value || !qtyInput.value) return alert("Must provide price and quantity");
    let duration = "";
    if (dayOption.checked) {
        duration = "Day";
    } else {
        duration = "ImmediateOrCancel";
    }

    let price_multiplier = g_current_products[g_current_product_id].price_divisor;
    const newOrder = {
        time: new Date().toLocaleTimeString(),
        product: g_current_product_id,
        side: side,
        price: Math.round(parseFloat(priceInput.value).toFixed(2) * price_multiplier),
        qty: qtyInput.value,
        duration: duration,
        client_order_id: Date.now(),
        status: 'Pending'
    };

    // Add to local state
    g_user_orders.unshift(newOrder); // Add to beginning of array
    renderUserOrders();

    fetch("api/order", {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify(newOrder),
    })
}

function renderUserOrders() {
    const body = document.getElementById('user-orders-body');

    function make_action_btn_html(order) {
        let action_btn_html;
        if (order.status == "Dead" || order.status == "Filled" || order.status.includes("Rejected")) {
            action_btn_html = `<td><button class="cancel-btn" onclick="clearOrder(${order.client_order_id})">Clear</button></td>`
        } else {
            action_btn_html = `<td><button class="cancel-btn" onclick="cancelOrder(${order.client_order_id})">Cancel</button></td>`
        }
        return action_btn_html;
    };

    body.innerHTML = g_user_orders.map(order => `
        <tr>
            ${make_action_btn_html(order)}
            <td>${order.client_order_id}</td>
            <td class="status-${order.status}">${order.status}</td>
            <td>${g_current_products[order.product.toString()].ticker}</td>
            <td style="color: ${order.side === 'BUY' ? 'var(--up-color)' : 'var(--down-color)'}">${order.side}</td>
            <td>$${order.price / g_current_products[order.product.toString()].price_divisor}</td>
            <td>${order.qty}</td>
            <td>${order.duration}</td>
            <td>${order.time}</td>
        </tr>
    `).join('');
}

function renderUserTrades() {
    const body = document.getElementById('user-trades-body');
    body.innerHTML = g_user_trades.map(trade => `
        <tr>
            <td>${trade.funx_transaction_id}</td>
            <td>${trade.client_order_id}</td>
            <td>${trade.fee_code}</td>
            <td>${g_current_products[trade.product.toString()].ticker}</td>
            <td style="color: ${trade.side === 'BUY' ? 'var(--up-color)' : 'var(--down-color)'}">${trade.side}</td>
            <td>$${trade.price / g_current_products[trade.product.toString()].price_divisor}</td>
            <td>${trade.qty}</td>
            <td>${trade.funx_order_id}</td>
            <td>${trade.funx_event_timestamp}</td>
        </tr>
    `).join('');
}

function cancelOrder(orderId) {
    const cancelOrder = {
        client_order_id: orderId,
    };
    fetch("api/order", {
        method: 'DELETE',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify(cancelOrder),
    })

    let order = g_user_orders.find(element => element.client_order_id == orderId);
    order.status = "PendingCancel";
    renderUserOrders();
}

function clearOrder(orderId) {
    g_user_orders = g_user_orders.filter(o => o.client_order_id !== orderId);
    renderUserOrders();
}

function hideUserTableContents() {
    let orders = document.getElementById('user-orders-table');
    orders.classList.add("user-data-hidden");
    let orders_btn = document.getElementById('show-orders-btn');
    orders_btn.classList.remove("active");

    let trades = document.getElementById('user-trades-table');
    trades.classList.add("user-data-hidden");
    let trades_btn = document.getElementById('show-trades-btn');
    trades_btn.classList.remove("active");
}

function showUserOrders() {
    hideUserTableContents();
    let orders = document.getElementById('user-orders-table');
    orders.classList.remove("user-data-hidden");
    let orders_btn = document.getElementById('show-orders-btn');
    orders_btn.classList.add("active");
}

function showUserTrades() {
    hideUserTableContents();
    let trades = document.getElementById('user-trades-table');
    trades.classList.remove("user-data-hidden");
    let trades_btn = document.getElementById('show-trades-btn');
    trades_btn.classList.add("active");
}

function setUpWebSocket() {
    g_websocket = new WebSocket("ws://" + window.location.host + "/ws")

    g_websocket.onopen = function(e) {
        g_websocket.send("Hello world");
    };

    g_websocket.onmessage = function(event) {
        const msg_json = JSON.parse(event.data);
        if (msg_json.msg_type == "OrderAccepted") {
            let order = g_user_orders.find(element => element.client_order_id == msg_json.client_order_id);
            if (order !== undefined) {
                order.status = "Active";
                renderUserOrders();
            }
        } else if (msg_json.msg_type == "OrderRemoved") {
            let order = g_user_orders.find(element => element.client_order_id == msg_json.client_order_id);
            if (order !== undefined) {
                if (order.status != "Filled") {
                    order.status = "Dead";
                    order.qty = 0;
                }
                renderUserOrders();
            }
        } else if (msg_json.msg_type == "Trade") {
            let order = g_user_orders.find(element => element.client_order_id == msg_json.client_order_id);
            if (order !== undefined) {
                order.qty -= msg_json.qty;
                if (order.qty > 0) {
                    order.status = "PartialFilled";
                } else {
                    order.status = "Filled";
                }
                renderUserOrders();
            }
            g_user_trades.unshift(msg_json);
            renderUserTrades();
        } else if (msg_json.msg_type == "OrderReject") {
            let order = g_user_orders.find(element => element.client_order_id == msg_json.client_order_id);
            if (order !== undefined) {
                order.status = `Rejected(${msg_json.reason})`;
                renderUserOrders();
            }
        } else if (msg_json.msg_type == "CancelReject") {
            let order = g_user_orders.find(element => element.client_order_id == msg_json.client_order_id);
            if (order !== undefined) {
                order.status = `CancelRejected(${msg_json.reason})`;
                renderUserOrders();
            }
        } else if (msg_json.msg_type == "Snapshot") {
            let product = msg_json.product;
            g_last_market_data_snapshot[product.toString()] = msg_json;
            if (product.toString() == g_current_product_id) {
                updateOrderBook();
            }
        }
    };
}

// Initial Load
loadSymbolsInUI();
updateOrderBook();
setUpWebSocket();

