#include <iostream>
#include <string.h>
#include <fstream>
#include <vector>
#include <omp.h>
#include <math.h>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>


void processPixels(int start, int end, float division, int minY, std::vector<unsigned char> &pixels)
{
    for(int i = start; i < end; i++)
    {
        int oldY = pixels[i];
        int newY =(oldY - minY) * division; 
        pixels[i] = static_cast<unsigned char>(std::max(0, std::min(255, newY)));
    }    
}

void processPixels(std::atomic<size_t> &currentByte, size_t pixelCountAll, size_t chunkSize, float division, int minY, std::vector<unsigned char> &pixels)
{
    while(true)
    {
        const size_t start = currentByte.fetch_add(chunkSize);
            if(start >= pixelCountAll) break;
            const size_t end = std::min(start + chunkSize, pixelCountAll);


        for(size_t i = start; i < end; i++)
        {
            int oldY = pixels[i];
            int newY =(oldY - minY) * division; 
            pixels[i] = static_cast<unsigned char>(std::max(0, std::min(255, newY)));
        }  
    } 
}

void getMinMaxValues(int ignorePerChannel, int histogram[], int &minY, int &maxY)
{
    int sum1 = 0;
        for(int i = 0; i < 256; i++)
        {
            sum1 += histogram[i];
            if(sum1 >= ignorePerChannel && sum1 != 0)
            {
                minY = i;
                break;
            }
        }
        
        sum1 = 0;
        for(int i = 255; i >= 0; i--)
        {
            sum1+= histogram[i];
            if(sum1 >= ignorePerChannel && sum1 != 0)
            {
                maxY = i; 
                break;
            }
        }
};

void processHistogramsP6( std::vector<unsigned char> &pixels, size_t start, size_t end, 
                        int (&histogram1)[256], int (&histogram2)[256], int (&histogram3)[256], std::mutex& merge_mutex){

    int localHistogram1[256] = {0};
    int localHistogram2[256] = {0};
    int localHistogram3[256] = {0};

    for (size_t i = start; i < end; i += 3)
    {
        localHistogram1[(int)pixels[i]]++;
        localHistogram2[(int)pixels[i+1]]++;
        localHistogram3[(int)pixels[i+2]]++;
    }

    
    std::lock_guard<std::mutex> lock(merge_mutex);
        for (int j = 0; j < 256; j++)
        {
            histogram1[j] += localHistogram1[j];
            histogram2[j] += localHistogram2[j];
            histogram3[j] += localHistogram3[j];
        }  
};

void processHistogramsP6( std::vector<unsigned char> &pixels, size_t pixelCount, std::atomic<size_t> &currentPixel,
    size_t chunkSize, int (&histogram1)[256], int (&histogram2)[256], int (&histogram3)[256], std::mutex& merge_mutex){

    int localHistogram1[256] = {0};
    int localHistogram2[256] = {0};
    int localHistogram3[256] = {0};

    while(true)
    {
    const size_t startPixel = currentPixel.fetch_add(chunkSize);

    if(startPixel >= pixelCount) break;

    const size_t endPixel = std::min(startPixel + chunkSize, pixelCount);
    const size_t start = startPixel * 3;
    const size_t end = endPixel * 3;

    for (size_t i = start; i < end; i += 3)
    {
    localHistogram1[(int)pixels[i]]++;
    localHistogram2[(int)pixels[i+1]]++;
    localHistogram3[(int)pixels[i+2]]++;
    }
    }

    std::lock_guard<std::mutex> lock(merge_mutex);
    for (int j = 0; j < 256; j++)
    {
    histogram1[j] += localHistogram1[j];
    histogram2[j] += localHistogram2[j];
    histogram3[j] += localHistogram3[j];
    }  
};

void processHistogramsP5( std::vector<unsigned char> &pixels, size_t start, size_t end, 
                        int (&histogram1)[256], std::mutex& merge_mutex){

    int localHistogram1[256] = {0};

    for (size_t i = start; i < end; i++)
    {
        localHistogram1[(int)pixels[i]]++;
    }

    std::lock_guard<std::mutex> lock(merge_mutex);
        for (int j = 0; j < 256; j++)
        {
            histogram1[j] += localHistogram1[j];
        }  
};

void processHistogramsP5( std::vector<unsigned char> &pixels, size_t pixelCount, std::atomic<size_t> &currentPixel,
    size_t chunkSize, int (&histogram1)[256], std::mutex& merge_mutex){

    int localHistogram1[256] = {0};

    while(true)
    {
        const size_t start = currentPixel.fetch_add(chunkSize);
        if(start >= pixelCount) break;
        const size_t end = std::min(start + chunkSize, pixelCount);


        for (size_t i = start; i < end; i ++)
        {
            localHistogram1[(int)pixels[i]]++;
            
        }
    }

    std::lock_guard<std::mutex> lock(merge_mutex);
        for (int j = 0; j < 256; j++)
        {
            histogram1[j] += localHistogram1[j];
        }  
};

int main(int argc, char** argv)
{
    std::string inputFile, outputFile;
    float coef = 0.0;
    int numThreads = -1;

    if(argc != 8 && argc != 9)
    {   std::cerr<< "Invalid input data." << '\n'
                 << " Usage :  --input <file> --output <file> --coef <value> [--no-cpp-threads|--cpp-threads <num|default>]\n";
                 return 1;
    }

    for(int i = 1; i < argc; i++) 
    {
      if (!strcmp(argv[i],"--input")) inputFile = argv[i + 1];
      else if (!strcmp(argv[i],"--output")) outputFile = argv[i + 1];
      else if (!strcmp(argv[i], "--coef")) coef = static_cast<float>(atof(argv[i + 1]));
      else if (!strcmp(argv[i], "--no-cpp-threads")) numThreads = 1;
      else if (!strcmp(argv[i], "--cpp-threads")){
        if(!strcmp(argv[i + 1],"default")) numThreads = std::thread::hardware_concurrency();
        else numThreads = atoi(argv[i + 1]);
      } 
    }
    if(coef < 0.0f || coef >= 0.5f)
    {
        std::cerr << "Error - Incorrect value of Coefficient. Range - [0.0,0.5] \n";
        return 1;
    }

    if(numThreads == -1) 
    {
        std::cerr << "Error - Parralelism mode not set correctly \n Usage: --no-cpp-threads | --cpp-threads [num_threads | default]";
        return 1; 
    }

    std::ifstream file(inputFile, std::ios::binary);
    if (!file)
    {
        std::cerr << "Error while opening the input file!\n";
        return 1;
    }

    std::string format;
    int width, height, max_val; 
    file >> format >> width >> height >> max_val;

    if (max_val != 255)
    {
        std::cerr << "Error! Only 8-bit images (max_val = 255) are supported.\n";
        return 1;
    }

    if(format != "P6" && format != "P5")
    {
        std::cerr << "Error! unsupported file format ( supports P6 and P5 format with max values of 256)";
        return 1;
    }
    file.ignore(); 


    
    size_t pixelCountAll = (format == "P6") ? 
    static_cast<size_t>(width) * height * 3 : 
    static_cast<size_t>(width) * height;

    size_t pixelCount = static_cast<size_t>(width) * height; // maybe used for pixel alligment?

    std::vector<unsigned char> pixels(pixelCountAll);
    file.read(reinterpret_cast<char*>(pixels.data()), pixels.size());
    file.close();

    std::atomic<size_t> currentPixel(0);
    std::mutex merge_mutex;
    std::vector<std::thread> threads;
    size_t chunkSize = 9999;


    int histogram1[256] = {0};
    int histogram2[256] = {0};
    int histogram3[256] = {0};
    int maxY, minY;
    size_t ignorePerChannel = static_cast<size_t>(width * height * coef);


    char mode = 's'; // s = static d = dynamic
    volatile double tstart = omp_get_wtime();

    if (format == "P6")
    {
       
        if (numThreads > 1) 
        {   
            if(mode == 's')
            {
                size_t basePixel = pixelCount / numThreads;
                int extraPixel = pixelCount % numThreads;

                for (int i = 1; i < numThreads; ++i) 
                {
                    size_t startPixel = i * basePixel + std::min(i, extraPixel);
                    size_t endPixel = startPixel + basePixel + (i < extraPixel ? 1 : 0);
                    size_t startByte = startPixel * 3;
                    size_t endByte = endPixel * 3;
            
                    threads.emplace_back([&, startByte, endByte](){
                        processHistogramsP6(pixels, startByte, endByte, histogram1, histogram2, histogram3, merge_mutex);
                    });         
                }
                size_t endPixelMain = basePixel + (0 < extraPixel ? 1 : 0);
                processHistogramsP6(pixels, 0, endPixelMain * 3, histogram1, histogram2, histogram3, merge_mutex);

                for (auto& t : threads) t.join();
                threads.clear();
            }
            else if (mode == 'd')
            {
                std::cout << " dinamik";
                for (int i = 1; i < numThreads; ++i) {
               
                    threads.emplace_back([&, chunkSize](){
                        processHistogramsP6(pixels, pixelCount, currentPixel, chunkSize, histogram1, histogram2, histogram3, merge_mutex);
                    });         
                }
                processHistogramsP6(pixels, pixelCount, currentPixel, chunkSize, histogram1, histogram2, histogram3, merge_mutex);
    
                for (auto& t : threads) t.join();
                threads.clear();
            }
            
        }
        else
        {
            for (size_t i = 0; i < pixelCountAll; i += 3)
            {
                histogram1[pixels[i]]++;
                histogram2[pixels[i+1]]++;
                histogram3[pixels[i+2]]++;
            }
        }

       
    
        int min1Y = 0, max1Y = 0;
        getMinMaxValues(ignorePerChannel,histogram1, min1Y, max1Y);

        int min2Y, max2Y;
        getMinMaxValues(ignorePerChannel, histogram2, min2Y, max2Y);

        int min3Y, max3Y;
        getMinMaxValues(ignorePerChannel, histogram3, min3Y, max3Y);

        minY = std::min(min1Y, std::min(min2Y, min3Y));
        maxY = std::max(max1Y, std::max(max2Y, max3Y));

    }
    else 
    {
        if (numThreads > 1) 
        {
            if(mode == 's')
            {
                size_t basePixel = pixelCountAll / numThreads;
                int extraPixel = pixelCountAll % numThreads;
                for (int i = 1; i < numThreads; ++i) {
                    size_t start = i * basePixel + std::min(i, (int)extraPixel);
                    size_t end = start + basePixel + (i < extraPixel ? 1 : 0);
                    
                    threads.emplace_back([&, start, end](){
                        processHistogramsP5(pixels, start, end, histogram1, merge_mutex);
                    });  
                }
                size_t endPixelMain = basePixel + (0 < extraPixel ? 1 : 0);
                processHistogramsP5(pixels, 0, endPixelMain , histogram1, merge_mutex);

                for (auto& t : threads) t.join();
                threads.clear();
            } 
            else if(mode == 'd')
            {
                for (int i = 1; i < numThreads; ++i) 
                {
                    threads.emplace_back([&, chunkSize](){
                        processHistogramsP5(pixels, pixelCountAll, currentPixel, chunkSize, histogram1, merge_mutex);
                    });  
                }
                processHistogramsP5(pixels, pixelCountAll, currentPixel, chunkSize, histogram1, merge_mutex);

                for (auto& t : threads) t.join();
                threads.clear();
            }
            
        }
        else
        {
            for (size_t i = 0; i < pixelCountAll; i++ )
            {
                histogram1[pixels[i]]++;
            }
        }
        
        getMinMaxValues(ignorePerChannel, histogram1, minY, maxY);
    }
    
    
    float division = 255.0f / (maxY - minY);
    chunkSize = 10000;
    std::atomic<size_t> currentByte(0);
    size_t basePixel = pixelCountAll / numThreads;
    size_t extraPixel = pixelCountAll % numThreads;

    if(mode == 's')
    {
        for(int i = 1; i < numThreads; i++)
        {
            size_t startPixel = i * basePixel + std::min(i, (int)extraPixel);
            size_t endPixel = startPixel + basePixel + (i < extraPixel ? 1 : 0);
            size_t start = startPixel;
            size_t end = endPixel;

            threads.emplace_back([&, start, end](){
                processPixels(start, end, division, minY, pixels);
            });
            }
        size_t endPixelMain = basePixel + (0 < extraPixel ? 1 : 0);
        processPixels(0, endPixelMain, division, minY, pixels);

        for(auto& th : threads) th.join();
    }
    else if(mode == 'd')
    {
        for(int i = 1; i < numThreads; i++)
        {
            threads.emplace_back([&, chunkSize](){
                processPixels(currentByte, pixelCountAll, chunkSize, division, minY, pixels);
            });
        }
         processPixels(currentByte, pixelCountAll, chunkSize, division, minY, pixels);

        for(auto& th : threads) th.join();
    }
    

    volatile double tend = omp_get_wtime();
    printf("Time (%i threads): %lg\n", numThreads,(tend - tstart) * 1000 );

    std::ofstream outFile(outputFile, std::ios::binary);
    outFile << format << "\n" << width << " " << height << "\n255\n";
    outFile.write(reinterpret_cast<char*>(pixels.data()), pixels.size());
    outFile.close();


    return 0;
}

