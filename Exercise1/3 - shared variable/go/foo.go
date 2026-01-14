// Use `go run foo.go` to run your program

package main

import (
	. "fmt"
	"runtime"
)

var i = -1

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

func main() {
	// What does GOMAXPROCS do? What happens if you set it to 1?
	// Tested, did not observe difference when using time to check performance
	runtime.GOMAXPROCS(2)
	iChan := make(chan bool, 1000000)
	dChan := make(chan bool, 1000000)
	doneChan := make(chan bool, 2)

	go incrementing(iChan, doneChan)
	go decrementing(dChan, doneChan)

	done := 2
	for {
		select {
		case <-iChan:
			i++
		case <-dChan:
			i--
		case <-doneChan:
			done--
			if done == 0 {
				// Drain any remaining messages from both channels
				for {
					select {
					case <-iChan:
						i++
					case <-dChan:
						i--
					default:
						Println("The magic number is:", i)
						return
					}
				}
			}
		}
	}
}
