# Low Latency Order Book in C++
This project implements a simulated low latency order book in C++. It does this by using data structures and algorithms optimized for fast access and updates, such as hash maps and queues.

*Really this project is to demonstrate that I can use C++ for low latency applications and object oriented programming. - as I don't really have a project that shows that right now.*
## Features
- Order book management with bid and ask sides
- Fast order matching and execution
- Support for limit and market orders (more to be added)
- right now processes about 3 million orders per second (high-end PC)

## to do
### interms of features
- Add support for different order types (stop loss, IoC...)
- Add a server to handle incoming orders via network
- add  user class to manage multiple users and their portfolios
- save order to DB or file
- GUI???

### interms of performance (the better stuff)
- Maybe use cache friendly data structures like flat_map 
- multi-thread the whole thing (when network / users are added)

