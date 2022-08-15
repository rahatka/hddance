/*
    (C) 2022 Ihar Rahatka

    This program is for comparison analysis only.
    Results may vary depending on kernel, IDE/SATA controller driver, system load, timer impl, etc.

    This program uses head movement patterns from another software:
        hdmotion - Moves hard disk heads in interesting patterns
        (C) 2005 by Jeremy Stanley

        This program may be distributed freely provided this notice is preserved.

        The author assumes no liability for any damages arising
        out of the use of this program, including but not limited
        to loss of data or desire to open up operational hard drives.
*/

#include <string.h>
#include <string>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <signal.h>
#include <atomic>
#include <random>
#include <chrono>
#include <algorithm>
#include <linux/hdreg.h>
#include <boost/algorithm/string/trim.hpp>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

constexpr size_t SAMPLES = 2048;
constexpr size_t BLOCK = 512;
constexpr size_t KiB = 1LLU << 10;
constexpr size_t MiB = 1LLU << 20;

static std::atomic<bool> quit(false);
static bool printpos = false;

using result_t = std::vector<std::pair<double, double>>;
using uniform_t = std::uniform_real_distribution<double>;
using exponential_t = std::exponential_distribution<>;

class normal_exit : std::exception
{
};

void got_signal(int)
{
    quit.store(true);
}

class hddance
{
public:
    std::string name;
    std::string results_dir;
    size_t block;
    size_t capacity;

    hddance(const std::string &dev, const std::string &res) : 
        name(""),
        results_dir(res),
        block(0),
        capacity(0),
        dev(dev),
        fd(-1),
        buf(nullptr),
        rd(),
        generator(rd())
    {
        // this->block = std::stoi(block);
        this->fd = open(dev.c_str(), O_RDONLY | O_DIRECT);
        if (this->fd == -1)
        {
            throw std::runtime_error("can't open block device");
        }
        this->name = get_hdd_name();
        buf = static_cast<char*>(std::aligned_alloc(BLOCK, MiB));
    }

    ~hddance()
    {
        if (fd >= 0)
        {
            close(fd);
        }
        std::free(buf);
    }

    void get_capacity()
    {
        if (ioctl(fd, BLKGETSIZE64, &capacity) != 0)
        {
            throw std::runtime_error("can't get disk capacity");
        }
    }

    void set_block_size(const size_t &block)
    {
        if (block > MiB)
        {
            throw std::runtime_error("block can't be more than 1 MiB");
        }
        std::cout << "setting block size to " << block << " Bytes";
        this->block = block;
        get_capacity();
        this->capacity -= this->block;
    }

    double read_position(double position)
    {
        using namespace std::chrono;

        if (quit.load())
        {
            throw normal_exit();
        }
        if (position < 0)
        {
            position = 0;
        }
        else if (position > 1)
        {
            position = 1;
        }

        if (printpos){
            std::string line(80, ' ');
            line[(uint8_t)(position * 79)] = '#';
            std::cout << line << std::endl;
        }

        size_t by = size_t(capacity * position) / this->block * this->block;
        lseek(fd, by, SEEK_SET);
        auto t1 = high_resolution_clock::now();
        auto got = read(fd, buf, this->block);
        auto t2 = high_resolution_clock::now();
        if (got != ssize_t(block))
        {
            std::cerr << "read_position: expected/got bytes mismatch, might be a bad sector" << std::endl;
        }
        return duration_cast<duration<double>>(t2 - t1).count();
    }

    std::string get_hdd_name()
    {
        auto wstrim = [](unsigned char *str, const size_t &size) -> std::string
        {
            std::string result;
            for (size_t i = 0; i < size; ++i)
            {
                if (str[i] != '\0')
                {
                    result.push_back(str[i]);
                }
            }
            boost::algorithm::trim(result);
            return result;
        };

        static struct hd_driveid hd;
        std::string hdmodel, fw_rev, serial;

        get_capacity();
        if (!ioctl(fd, HDIO_GET_IDENTITY, &hd))
        {
            hdmodel = wstrim(hd.model, sizeof(hd.model));
            fw_rev = wstrim(hd.fw_rev, sizeof(hd.fw_rev));
            serial = wstrim(hd.serial_no, sizeof(hd.serial_no));
            std::cout << "Hard Disk Model: " << hdmodel << std::endl;
            std::cout << "Serial Number: " << serial << std::endl;
            std::cout << "Firmware Revision: " << fw_rev << std::endl << std::endl;
        }
        else if (errno == -ENOMSG)
        {
            std::cerr << "no hard disk identification information available" << std::endl;
            return std::to_string(capacity / MiB) + "_MiB_drive";
        }
        else
        {
            std::cerr << "error getting disk identity" << std::endl;
            return std::to_string(capacity / MiB) + "_MiB_drive";
        }
        if (hdmodel.empty())
        {
            return std::to_string(capacity / MiB) + "_MiB_drive";
        }
        return hdmodel;
    }

    void perform_random_read_benchmark(const size_t &block_size)
    {
        exponential_t exp_distr;
        set_block_size(block_size);
        result_t results;
        double total_seconds = 0;

        std::vector<double> precomp;
        precomp.reserve(SAMPLES);
        while (precomp.size() < SAMPLES)
        {
            auto pos = exp_distr(generator) / M_E;
            if (pos <= 1.0)
            {
                precomp.push_back(pos);
            }
        }

        std::cout << std::endl;

        size_t counter = 0;
        for (const auto &x : precomp)
        {
            results.push_back(result_t::value_type(x, read_position(x)));
            auto line = std::to_string(++counter);
            std::cout << line << "\r" << std::flush;
        }
        std::cout << std::endl;

        std::ofstream results_file(results_dir + name + "_" + std::to_string(block) + ".txt");
        if (results_file.is_open())
        {
            results_file << name << std::endl;
            results_file << block << std::endl;
            for (auto &x : results)
            {
                if (x.second <= 0) {
                    throw std::runtime_error("invalid block access measurement, repeat the test");
                }
                results_file << x.first << ":" << x.second << std::endl;
            }
            results_file.close();
        }
        else
        {
            throw std::runtime_error("can't open results file");
        }
        for (const auto &x : results)
        {
            total_seconds += x.second;
        }
        std::cout << "average read access time for " << block_size << " B block is " << total_seconds / results.size() * 1000 << " ms" << std::endl;
    }

    void move_heads()
    {
        set_block_size(BLOCK);

        double f, s, l, h, amp;
        int i, heads;
        uniform_t zto_distr(0, 1);
        uniform_t pos_distr(0, 0.0001);
        uniform_t cen_distr(-0.00005, 0.00005);

        // accelerating zigzag
        s = 0.010;
        for (i = 0; i < 5; ++i)
        {
            for (f = 0.0; f < 1.0; f += s)
                read_position(f + cen_distr(generator));
            for (f -= s; f > 0.0; f -= s)
                read_position(f + cen_distr(generator));
            s += 0.0075;
        }
        f += s;

        // tightening zigzag
        h = 0.90;
        l = 0.10;
        for (; l < h;)
        {
            for (; f < h; f += s)
                read_position(f + cen_distr(generator));
            for (; f > l; f -= s)
                read_position(f + cen_distr(generator));
            h -= 0.05;
            l += 0.05;
        }

        // widening sinusoid
        amp = 0.05;
        for (; amp <= 0.50; amp += 0.05)
        {
            double x;
            for (x = 0; x < (2 * M_PI); x += M_PI / 32.0)
            {
                read_position((sin(x) * amp) + 0.5 + cen_distr(generator));
            }
        }

        // narrowing sinusoid
        for (amp = 0.50; amp > 0.0; amp -= 0.05)
        {
            double x;
            for (x = 0; x < (2 * M_PI); x += M_PI / 32.0)
            {
                read_position((sin(x) * amp) + 0.5 + cen_distr(generator));
            }
        }

        // widening double-sinusoid
        amp = 0.05;
        for (; amp <= 0.50; amp += 0.05)
        {
            double x;
            for (x = 0; x < (2 * M_PI); x += M_PI / 16.0)
            {
                f = (sin(x) * amp) + 0.5;
                read_position(f + cen_distr(generator));
                read_position(1.0 - f - cen_distr(generator));
            }
        }

        // narrowing double-sinusoid
        for (amp = 0.50; amp > 0.0; amp -= 0.05)
        {
            double x;
            for (x = 0; x < (2 * M_PI); x += M_PI / 16.0)
            {
                f = (sin(x) * amp) + 0.5;
                read_position(f + cen_distr(generator));
                read_position(1.0 - f - cen_distr(generator));
            }
        }

        // buncha heads
        for (heads = 2; heads < 7; ++heads)
        {
            int repeat = 160 / heads;
            for (int i = 0; i < repeat; ++i)
            {
                for (int j = 1; j <= heads; ++j)
                {
                    read_position((double)j / (heads + 1) + cen_distr(generator));
                }
            }
        }
        for (; heads > 0; heads -= 2)
        {
            int repeat = 160 / heads;
            for (int i = 0; i < repeat; ++i)
            {
                for (int j = 1; j <= heads; ++j)
                {
                    read_position((double)j / (heads + 1) + cen_distr(generator));
                }
            }
        }

        // max
        std::vector<double> ts;
        for (i = 0; i < 200; ++i)
        {
            ts.push_back(read_position(0 + pos_distr(generator)));
            ts.push_back(read_position(1 - pos_distr(generator)));
        }
        std::cout << "average full swing is " << std::reduce(ts.begin(), ts.end(), 0.0) / ts.size() * 1000 << " ms" << std::endl;
    }

private:
    std::string dev;
    int fd;
    char* buf;
    std::random_device rd;
    std::default_random_engine generator;
};

int main(int argc, char **argv)
{
    namespace po = boost::program_options;

    try
    {
        std::string device, output;
        size_t block;
        po::options_description desc("Allowed options");
        desc.add_options()
            ("device", po::value(&device), "A device to test")
            ("help,h", "Print usage")
            ("version,v", "Print version")
            ("moveheads,m", "Move heads")
            ("blocksize,b", po::value(&block), "Set random test block size in KiB")
            ("output,o", po::value(&output), "Output dir for results")
            ("printposition,p", "Print head position");
        po::positional_options_description positionals;
        positionals.add("device", -1);
        po::variables_map vm;
        po::store(po::command_line_parser(argc, argv)
                    .positional(positionals)
                    .options(desc)
                    .run(),
                vm);
        po::notify(vm);
        if (vm.count("version"))
        {
            std::cout << "1.0" << std::endl;
            return 0;
        }
        if (vm.count("help"))
        {
            std::cout << desc << std::endl;
            return 0;
        }
        if (vm.count("printposition"))
        {
            printpos = true;
        }
        if (vm.count("output"))
        {
            if (output.back() != '/') {
                output += '/';
            }
            if (!boost::filesystem::exists(output)) {
                boost::filesystem::create_directory(output);
            }
        }

        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = got_signal;
        sigfillset(&sa.sa_mask);
        sigaction(SIGINT, &sa, nullptr);

        auto dance = hddance(device, output);
        if (vm.count("moveheads"))
        {
            dance.move_heads();
            return 0;
        }
        if (vm.count("blocksize"))
        {
            dance.perform_random_read_benchmark(block * KiB);
            return 0;
        }
        dance.perform_random_read_benchmark(BLOCK);
        dance.perform_random_read_benchmark(4 * KiB);
        dance.perform_random_read_benchmark(64 * KiB);
        return 0;
    }
    catch (const normal_exit &e)
    {
        std::cout << "canceled by user" << std::endl;
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
