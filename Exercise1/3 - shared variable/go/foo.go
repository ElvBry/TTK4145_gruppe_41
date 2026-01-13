// Use `go run foo.go` to run your program

package main

import (
	. "fmt"
	"runtime"
	"time"
)

var i = 0

func incrementing(ch, doneChan chan bool) {
	for range 1000000 {
		ch <- true
	}
	doneChan <- true
}

func decrementing(ch, doneChan chan bool) {
	//TODO: decrement i 1000000 times
	for range 1000000 {
		ch <- true
	}
	doneChan <- true
}

func server(iChan, deChan, doneChan chan bool) {
	for {
		select {
		case <-iChan:
			i++
		case <-deChan:
			i--
		}
	}
}

func main() {
	// What does GOMAXPROCS do? What happens if you set it to 1?
	runtime.GOMAXPROCS(2)
	iChan := make(chan bool)
	dChan := make(chan bool)
	doneChan := make(chan bool)

	go incrementing(iChan)
	decrementing(dChan)

	// We have no direct way to wait for the completion of a goroutine (without additional synchronization of some sort)
	// We will do it properly with channels soon. For now: Sleep.
	time.Sleep(500 * time.Millisecond)
	Println("The magic number is:", i)
}
