# **Encrypted Text Editor**

## Not Siblings, but Kangs

## *Joon Kang, Mari Kang*

## *Goals for the Project*

The goal of this project is to create a functioning text editor that encrypts and decrypts a text file when it is created and saved. To achieve this goal, we need two separate components - the first is to create a text editor that can create, edit and save text documents. The second part of this project will implement an encryption system that will encrypt all text files the editor creates, edits and saves. The lower bound of this project will be the creation of the text editor, and the completion of it's functionalities. If this is met and we have the capacity to go further, we will implement a system whee the text within the file is automatically encrypted when it is saved so that no other text editor other than this one can open it.

## *Learning Goals from the Project*

The learning goals of this project is to understand how C works in a broader spectrum, as well as being able to learn more about how C works with encryption and decryption. Working on this project with a structured C with header files and clean code will allow us to work on projects and code in a more structured and balanced manner. Also, working on cryptography will open up a new realm of learning for us as we do not know a lot about this topic. We know the very basics of cryptography and its diverse methods, so we would like to focus on learning more about it and its implementation in C.

## *Resources*

We need to find resources that will let us understand cryptography and its implementation in C. Also, we need to know how to create a text editor in C, so string management in a terminal setting would be a good resource to have, as well as file management.

[Build Own Text Editor](https://viewsourcecode.org/snaptoken/kilo/)
[A Simple Text Editor](https://github.com/kyletolle/texor)
[Encrypt and Decrypt String](http://www.trytoprogram.com/c-examples/c-program-to-encrypt-and-decrypt-string/)


## *What we have done*

We have managed to implement a text editor that runs in C with bare minimum functions of editing and saving. We have also managed to implement a function that allows the encryption of the content that is written in the text editor when it is saved. This way, the text editor will be able to save a file that no other text editors can comprehend. We used a simple ASCII code edit method for encryption and decryption, as other methods required a far more complicated and heavy computing methods of encryption. The program allows us to open/create a file, then save it with an automatic encryption. However the user of the text editor does not know this because the text editor always decrypts the files that it opens. All in all, the text file generated with this text editor cannot be viewed with any other text editors but this one.

//Add in code snippets for the function that encrypts
//Add in code that enables raw mode
//Add in code the writes line

## *Design Decisions*

One of the big design decisions that we have made throughout this project was the implementation of the encryption and decryption of the text files. It was possible to have a type of key or password that would be created for every text file to encrypt and decrypt, but we decided that this would be troublesome in terms of using the text editor, as the keys might get lost, and we have no way of retrieving that key. Our design decision was to implement a simple encryption that would allow a set key to adjust each individual character in the text file so that it would be incomprehensible to people who try to read it from a different text editor. Of course, this does come with a catch that this text editor is unable to read other text files from other text editors as well, because it would decrypt the files in the same way as it would with a file that the program generated. However we deemed this trivial because our goal was to create a text editor that could generate and read text files that only could be read by itself.

## *Vital Code Snippets*

//Add in code that enables raw mode
//Add in code the updates row
//Add in code that saves
//Add in code that opens/creates

## *Reflection*

In the end, our project was right where we wanted it to be. We managed to create 2 components to the project. The text editor works well in terms of being able to open/create text files, save the files and write to them as we would on a normal text editor. The encryption also works in terms of the texts in the document being incomprehensible for others to read once the document is saved. We have gone a step further by implementing the automation of the encryption, so that there is no extra steps in having to encrypt a file when saving the document. With that, we have achieved all the goals that we had in the goals for the project. In terms of our learning goals, we managed to understand a little more about how C works. We managed to find how to implement multiple different types of encryptions in C, although we did end up using the most simple method of encryption. Throughout our process, we discussed and tested with 3 different types of encryption and decryption - AES, RSA and ASCII replacement. We were able to study how each of these encryption/decryption worked in C, which was one of our learning goals. 

We would have liked to improve or add an extension to our existing project if we had more time, however. It would be great to see a method of choosing your own method of encryption, and a method to indicate whether or not the text is encrypted, so that other texts can also be read in this text editor program. Also, being able to implement other functions like searching would also be a huge extension that we would like to explore further on a later date.
