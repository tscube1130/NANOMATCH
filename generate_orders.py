import random
import csv

NUM_ORDERS = 5_000_000
FILENAME = "orders.csv"

def generate_data():
    print(f"Generating {NUM_ORDERS} orders (including cancellations)...")
    
    with open(FILENAME, mode='w', newline='') as file:
        writer = csv.writer(file)
        
        # We will keep a sliding window of the last 10,000 orders
        active_orders = [] 
        
        for order_id in range(1, NUM_ORDERS + 1):
            # 20% chance we generate a CANCEL order instead of an ADD order
            if active_orders and random.random() < 0.20:
                # Pick a random resting order to cancel
                cancel_index = random.randint(0, len(active_orders) - 1)
                cancel_id = active_orders.pop(cancel_index)
                
                # Format: [OrderID, Side (C), Price (0), Qty (0)]
                writer.writerow([cancel_id, 'C', 0, 0])
            
            else:
                side = 'B' if random.random() < 0.5 else 'S'
                if side == 'B':
                    price = random.randint(9000, 10000)
                else:
                    price = random.randint(10000, 11000)
                qty = random.randint(1, 100) * 10
                
                writer.writerow([order_id, side, price, qty])
                active_orders.append(order_id)
                
                # Keep memory usage light
                if len(active_orders) > 10000:
                    active_orders.pop(0)
                    
    print(f"Success! Saved to {FILENAME}")

if __name__ == "__main__":
    generate_data()