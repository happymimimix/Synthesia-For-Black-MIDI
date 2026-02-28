# Synthesia For Black MIDI
**Forked from Synthesia 0.6.1b**

**Made by: Khangaroo & Happy_mimimix**

## Demo video: 
https://youtu.be/QKMZrueUUu8

## What is this
It's Synthesia, but 128keys + 64bit + Autoplay!

## 64bit + 128 keys??? How is this even possible? 
Synthesia used to be a sign of very laggy and useless in Black MIDI. 

Mainly because it's 32bit and supports only up to 88 keys. 

The worst thing is that Synthesia is close sourced, which makes modifying it impossible. 

However, you may not've noticed that Synthesia WAS open sourced long ago! 

The last open sourced version is 0.6.1b. After that, it became close sourced and commersial. 

So, lets begin from here. 

Khangaroo downloaded the old source code and begins to make a 64 bit build. 

She also made some impovements on the performance of the software, it can now open large midi files much faster than ever before. 

Then, she made the keyboard support up to 128 keys! Wonderful. 

I was amazed by Khangaroo's creation. 

Although the process is simple, but her ability on being able to think of the idea of doing this is insane! 

It feels so good when something that used to be proven impossible has become POSSIBLE! 

Khangaroo's creation was amazing, but not amazing enough. 

I spot plenty of space for imporvement and decided to fill that up. 

First of all, synthesia hides all percussion tracks on default, which we don't want. 

If you wanna change this, you have to click on hidden tracks one by one to change it's statues, because synthesia back then does not have the bulk edit function. 

We want all tracks to be shown by default, so I changed that. 

Also, I added autoplay to Synthesia. It will treat all inputs as perfect if no midi input device is selected. 

**Hope you like my work    : )**

### A quick note: 
The score becomming -214783648 is caused by integer overflow, as Synthesia isn't made to be playing MIDIs with that large amount of notes. 

When the number exceeds the maximum value allowed by this variable type, in this case Synthesia used a 32bit unsigned integer, then it loops back to the begining, in other words the smallest number allowed by this variable type, which makes the score becomes -214783648. 

### Source code? 

SFBM is close sourced because I accidentally lost the source code... 

Sorry. 

#### Update 2024/12/14: I found it! 

## Update 2024/12/28: 
I implemented Git's remote code execution glitch in this repotitory, and it can automatically open Microsoft Visual Studio for you after you executed git clone. 

If you want to have a try, downgrade your Git to v2.45.0 and run ```git clone --recursive https://github.com/happymimimix/Synthesia-For-Black-MIDI.git```
