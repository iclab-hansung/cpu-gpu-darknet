#include "darknet.h"

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>

extern void predict_classifier(char *datacfg, char *cfgfile, char *weightfile, char *filename, int top);
extern void test_detector(char *datacfg, char *cfgfile, char *weightfile, char *filename, float thresh, float hier_thresh, char *outfile, int fullscreen);
extern void run_yolo(int argc, char **argv);
extern void run_detector(int argc, char **argv);
extern void run_coco(int argc, char **argv);
extern void run_nightmare(int argc, char **argv);
extern void run_classifier(int argc, char **argv);
extern void run_regressor(int argc, char **argv);
extern void run_segmenter(int argc, char **argv);
extern void run_isegmenter(int argc, char **argv);
extern void run_char_rnn(int argc, char **argv);
extern void run_tag(int argc, char **argv);
extern void run_cifar(int argc, char **argv);
extern void run_go(int argc, char **argv);
extern void run_art(int argc, char **argv);
extern void run_super(int argc, char **argv);
extern void run_lsd(int argc, char **argv);
extern void predict_classifier2(test *input);

void average(int argc, char *argv[])
{
    char *cfgfile = argv[2];
    char *outfile = argv[3];
    gpu_index = -1;
    network *net = parse_network_cfg(cfgfile);
    network *sum = parse_network_cfg(cfgfile);

    char *weightfile = argv[4];
    load_weights(sum, weightfile);

    int i, j;
    int n = argc - 5;
    for (i = 0; i < n; ++i)
    {
        weightfile = argv[i + 5];
        load_weights(net, weightfile);
        for (j = 0; j < net->n; ++j)
        {
            layer l = net->layers[j];
            layer out = sum->layers[j];
            if (l.type == CONVOLUTIONAL)
            {
                int num = l.n * l.c * l.size * l.size;
                axpy_cpu(l.n, 1, l.biases, 1, out.biases, 1);
                axpy_cpu(num, 1, l.weights, 1, out.weights, 1);
                if (l.batch_normalize)
                {
                    axpy_cpu(l.n, 1, l.scales, 1, out.scales, 1);
                    axpy_cpu(l.n, 1, l.rolling_mean, 1, out.rolling_mean, 1);
                    axpy_cpu(l.n, 1, l.rolling_variance, 1, out.rolling_variance, 1);
                }
            }
            if (l.type == CONNECTED)
            {
                axpy_cpu(l.outputs, 1, l.biases, 1, out.biases, 1);
                axpy_cpu(l.outputs * l.inputs, 1, l.weights, 1, out.weights, 1);
            }
        }
    }
    n = n + 1;
    for (j = 0; j < net->n; ++j)
    {
        layer l = sum->layers[j];
        if (l.type == CONVOLUTIONAL)
        {
            int num = l.n * l.c * l.size * l.size;
            scal_cpu(l.n, 1. / n, l.biases, 1);
            scal_cpu(num, 1. / n, l.weights, 1);
            if (l.batch_normalize)
            {
                scal_cpu(l.n, 1. / n, l.scales, 1);
                scal_cpu(l.n, 1. / n, l.rolling_mean, 1);
                scal_cpu(l.n, 1. / n, l.rolling_variance, 1);
            }
        }
        if (l.type == CONNECTED)
        {
            scal_cpu(l.outputs, 1. / n, l.biases, 1);
            scal_cpu(l.outputs * l.inputs, 1. / n, l.weights, 1);
        }
    }
    save_weights(sum, outfile);
}

long numops(network *net)
{
    int i;
    long ops = 0;
    for (i = 0; i < net->n; ++i)
    {
        layer l = net->layers[i];
        if (l.type == CONVOLUTIONAL)
        {
            ops += 2l * l.n * l.size * l.size * l.c / l.groups * l.out_h * l.out_w;
        }
        else if (l.type == CONNECTED)
        {
            ops += 2l * l.inputs * l.outputs;
        }
        else if (l.type == RNN)
        {
            ops += 2l * l.input_layer->inputs * l.input_layer->outputs;
            ops += 2l * l.self_layer->inputs * l.self_layer->outputs;
            ops += 2l * l.output_layer->inputs * l.output_layer->outputs;
        }
        else if (l.type == GRU)
        {
            ops += 2l * l.uz->inputs * l.uz->outputs;
            ops += 2l * l.uh->inputs * l.uh->outputs;
            ops += 2l * l.ur->inputs * l.ur->outputs;
            ops += 2l * l.wz->inputs * l.wz->outputs;
            ops += 2l * l.wh->inputs * l.wh->outputs;
            ops += 2l * l.wr->inputs * l.wr->outputs;
        }
        else if (l.type == LSTM)
        {
            ops += 2l * l.uf->inputs * l.uf->outputs;
            ops += 2l * l.ui->inputs * l.ui->outputs;
            ops += 2l * l.ug->inputs * l.ug->outputs;
            ops += 2l * l.uo->inputs * l.uo->outputs;
            ops += 2l * l.wf->inputs * l.wf->outputs;
            ops += 2l * l.wi->inputs * l.wi->outputs;
            ops += 2l * l.wg->inputs * l.wg->outputs;
            ops += 2l * l.wo->inputs * l.wo->outputs;
        }
    }
    return ops;
}

void speed(char *cfgfile, int tics)
{
    if (tics == 0)
        tics = 1000;
    network *net = parse_network_cfg(cfgfile);
    set_batch_network(net, 1);
    int i;
    double time = what_time_is_it_now();
    image im = make_image(net->w, net->h, net->c * net->batch);
    for (i = 0; i < tics; ++i)
    {
        network_predict(net, im.data);
    }
    double t = what_time_is_it_now() - time;
    long ops = numops(net);
    printf("\n%d evals, %f Seconds\n", tics, t);
    printf("Floating Point Operations: %.2f Bn\n", (float)ops / 1000000000.);
    printf("FLOPS: %.2f Bn\n", (float)ops / 1000000000. * tics / t);
    printf("Speed: %f sec/eval\n", t / tics);
    printf("Speed: %f Hz\n", tics / t);
}

void operations(char *cfgfile)
{
    gpu_index = -1;
    network *net = parse_network_cfg(cfgfile);
    long ops = numops(net);
    printf("Floating Point Operations: %ld\n", ops);
    printf("Floating Point Operations: %.2f Bn\n", (float)ops / 1000000000.);
}

void oneoff(char *cfgfile, char *weightfile, char *outfile)
{
    gpu_index = -1;
    network *net = parse_network_cfg(cfgfile);
    int oldn = net->layers[net->n - 2].n;
    int c = net->layers[net->n - 2].c;
    scal_cpu(oldn * c, .1, net->layers[net->n - 2].weights, 1);
    scal_cpu(oldn, 0, net->layers[net->n - 2].biases, 1);
    net->layers[net->n - 2].n = 11921;
    net->layers[net->n - 2].biases += 5;
    net->layers[net->n - 2].weights += 5 * c;
    if (weightfile)
    {
        load_weights(net, weightfile);
    }
    net->layers[net->n - 2].biases -= 5;
    net->layers[net->n - 2].weights -= 5 * c;
    net->layers[net->n - 2].n = oldn;
    printf("%d\n", oldn);
    layer l = net->layers[net->n - 2];
    copy_cpu(l.n / 3, l.biases, 1, l.biases + l.n / 3, 1);
    copy_cpu(l.n / 3, l.biases, 1, l.biases + 2 * l.n / 3, 1);
    copy_cpu(l.n / 3 * l.c, l.weights, 1, l.weights + l.n / 3 * l.c, 1);
    copy_cpu(l.n / 3 * l.c, l.weights, 1, l.weights + 2 * l.n / 3 * l.c, 1);
    *net->seen = 0;
    save_weights(net, outfile);
}

void oneoff2(char *cfgfile, char *weightfile, char *outfile, int l)
{
    gpu_index = -1;
    network *net = parse_network_cfg(cfgfile);
    if (weightfile)
    {
        load_weights_upto(net, weightfile, 0, net->n);
        load_weights_upto(net, weightfile, l, net->n);
    }
    *net->seen = 0;
    save_weights_upto(net, outfile, net->n);
}

void partial(char *cfgfile, char *weightfile, char *outfile, int max)
{
    gpu_index = -1;
    network *net = load_network(cfgfile, weightfile, 1);
    save_weights_upto(net, outfile, max);
}

void print_weights(char *cfgfile, char *weightfile, int n)
{
    gpu_index = -1;
    network *net = load_network(cfgfile, weightfile, 1);
    layer l = net->layers[n];
    int i, j;
    //printf("[");
    for (i = 0; i < l.n; ++i)
    {
        //printf("[");
        for (j = 0; j < l.size * l.size * l.c; ++j)
        {
            //if(j > 0) printf(",");
            printf("%g ", l.weights[i * l.size * l.size * l.c + j]);
        }
        printf("\n");
        //printf("]%s\n", (i == l.n-1)?"":",");
    }
    //printf("]");
}

void rescale_net(char *cfgfile, char *weightfile, char *outfile)
{
    gpu_index = -1;
    network *net = load_network(cfgfile, weightfile, 0);
    int i;
    for (i = 0; i < net->n; ++i)
    {
        layer l = net->layers[i];
        if (l.type == CONVOLUTIONAL)
        {
            rescale_weights(l, 2, -.5);
            break;
        }
    }
    save_weights(net, outfile);
}

void rgbgr_net(char *cfgfile, char *weightfile, char *outfile)
{
    gpu_index = -1;
    network *net = load_network(cfgfile, weightfile, 0);
    int i;
    for (i = 0; i < net->n; ++i)
    {
        layer l = net->layers[i];
        if (l.type == CONVOLUTIONAL)
        {
            rgbgr_weights(l);
            break;
        }
    }
    save_weights(net, outfile);
}

void reset_normalize_net(char *cfgfile, char *weightfile, char *outfile)
{
    gpu_index = -1;
    network *net = load_network(cfgfile, weightfile, 0);
    int i;
    for (i = 0; i < net->n; ++i)
    {
        layer l = net->layers[i];
        if (l.type == CONVOLUTIONAL && l.batch_normalize)
        {
            denormalize_convolutional_layer(l);
        }
        if (l.type == CONNECTED && l.batch_normalize)
        {
            denormalize_connected_layer(l);
        }
        if (l.type == GRU && l.batch_normalize)
        {
            denormalize_connected_layer(*l.input_z_layer);
            denormalize_connected_layer(*l.input_r_layer);
            denormalize_connected_layer(*l.input_h_layer);
            denormalize_connected_layer(*l.state_z_layer);
            denormalize_connected_layer(*l.state_r_layer);
            denormalize_connected_layer(*l.state_h_layer);
        }
    }
    save_weights(net, outfile);
}

layer normalize_layer(layer l, int n)
{
    int j;
    l.batch_normalize = 1;
    l.scales = calloc(n, sizeof(float));
    for (j = 0; j < n; ++j)
    {
        l.scales[j] = 1;
    }
    l.rolling_mean = calloc(n, sizeof(float));
    l.rolling_variance = calloc(n, sizeof(float));
    return l;
}

void normalize_net(char *cfgfile, char *weightfile, char *outfile)
{
    gpu_index = -1;
    network *net = load_network(cfgfile, weightfile, 0);
    int i;
    for (i = 0; i < net->n; ++i)
    {
        layer l = net->layers[i];
        if (l.type == CONVOLUTIONAL && !l.batch_normalize)
        {
            net->layers[i] = normalize_layer(l, l.n);
        }
        if (l.type == CONNECTED && !l.batch_normalize)
        {
            net->layers[i] = normalize_layer(l, l.outputs);
        }
        if (l.type == GRU && l.batch_normalize)
        {
            *l.input_z_layer = normalize_layer(*l.input_z_layer, l.input_z_layer->outputs);
            *l.input_r_layer = normalize_layer(*l.input_r_layer, l.input_r_layer->outputs);
            *l.input_h_layer = normalize_layer(*l.input_h_layer, l.input_h_layer->outputs);
            *l.state_z_layer = normalize_layer(*l.state_z_layer, l.state_z_layer->outputs);
            *l.state_r_layer = normalize_layer(*l.state_r_layer, l.state_r_layer->outputs);
            *l.state_h_layer = normalize_layer(*l.state_h_layer, l.state_h_layer->outputs);
            net->layers[i].batch_normalize = 1;
        }
    }
    save_weights(net, outfile);
}

void statistics_net(char *cfgfile, char *weightfile)
{
    gpu_index = -1;
    network *net = load_network(cfgfile, weightfile, 0);
    int i;
    for (i = 0; i < net->n; ++i)
    {
        layer l = net->layers[i];
        if (l.type == CONNECTED && l.batch_normalize)
        {
            printf("Connected Layer %d\n", i);
            statistics_connected_layer(l);
        }
        if (l.type == GRU && l.batch_normalize)
        {
            printf("GRU Layer %d\n", i);
            printf("Input Z\n");
            statistics_connected_layer(*l.input_z_layer);
            printf("Input R\n");
            statistics_connected_layer(*l.input_r_layer);
            printf("Input H\n");
            statistics_connected_layer(*l.input_h_layer);
            printf("State Z\n");
            statistics_connected_layer(*l.state_z_layer);
            printf("State R\n");
            statistics_connected_layer(*l.state_r_layer);
            printf("State H\n");
            statistics_connected_layer(*l.state_h_layer);
        }
        printf("\n");
    }
}

void denormalize_net(char *cfgfile, char *weightfile, char *outfile)
{
    gpu_index = -1;
    network *net = load_network(cfgfile, weightfile, 0);
    int i;
    for (i = 0; i < net->n; ++i)
    {
        layer l = net->layers[i];
        if ((l.type == DECONVOLUTIONAL || l.type == CONVOLUTIONAL) && l.batch_normalize)
        {
            denormalize_convolutional_layer(l);
            net->layers[i].batch_normalize = 0;
        }
        if (l.type == CONNECTED && l.batch_normalize)
        {
            denormalize_connected_layer(l);
            net->layers[i].batch_normalize = 0;
        }
        if (l.type == GRU && l.batch_normalize)
        {
            denormalize_connected_layer(*l.input_z_layer);
            denormalize_connected_layer(*l.input_r_layer);
            denormalize_connected_layer(*l.input_h_layer);
            denormalize_connected_layer(*l.state_z_layer);
            denormalize_connected_layer(*l.state_r_layer);
            denormalize_connected_layer(*l.state_h_layer);
            l.input_z_layer->batch_normalize = 0;
            l.input_r_layer->batch_normalize = 0;
            l.input_h_layer->batch_normalize = 0;
            l.state_z_layer->batch_normalize = 0;
            l.state_r_layer->batch_normalize = 0;
            l.state_h_layer->batch_normalize = 0;
            net->layers[i].batch_normalize = 0;
        }
    }
    save_weights(net, outfile);
}

void mkimg(char *cfgfile, char *weightfile, int h, int w, int num, char *prefix)
{
    network *net = load_network(cfgfile, weightfile, 0);
    image *ims = get_weights(net->layers[0]);
    int n = net->layers[0].n;
    int z;
    for (z = 0; z < num; ++z)
    {
        image im = make_image(h, w, 3);
        fill_image(im, .5);
        int i;
        for (i = 0; i < 100; ++i)
        {
            image r = copy_image(ims[rand() % n]);
            rotate_image_cw(r, rand() % 4);
            random_distort_image(r, 1, 1.5, 1.5);
            int dx = rand() % (w - r.w);
            int dy = rand() % (h - r.h);
            ghost_image(r, im, dx, dy);
            free_image(r);
        }
        char buff[256];
        sprintf(buff, "%s/gen_%d", prefix, z);
        save_image(im, buff);
        free_image(im);
    }
}

void visualize(char *cfgfile, char *weightfile)
{
    network *net = load_network(cfgfile, weightfile, 0);
    visualize_network(net);
}

void choiceNetwork()
{
}

twin_thpool *twin_thp;
threadpool time_check_thp;
//각 네트워크의 조건변수, mutex변수, wait를 위한 변수 선언 헤더에 extern변수로 지정
pthread_cond_t *cond_t;
pthread_mutex_t *mutex_t;
int *cond_i;

double sync_time_list[32];
int n_total;
double gpu_total_time = 0;
double cpu_total_time = 0;
double start_time_a;
double start_time;
int cpu_thread;
int gpu_thread;
int g=0, c=0;
int start_flag;

int main(int argc, char* argv[])
{
    if(argc != 7){
        fprintf(stderr, "execute example\n");
        fprintf(stderr, "./darknet <cpu_thread> <gpu_thread> <n_des> <n_res> <n_vgg> <n_alex>\n");
        return -1;
    }
#ifndef GPU
    gpu_index = -1;
#else
    gpu_index = 0;
    if (gpu_index >= 0)
    {
        cuda_set_device(gpu_index);
    }

    cudaSetDeviceFlags(cudaDeviceMapHost);
    cublasInit();
#endif
    cpu_thread = atoi(argv[1]);
    gpu_thread = atoi(argv[2]);
#ifdef THREAD
    twin_thp = twin_thpool_init(cpu_thread,gpu_thread);
    time_check_thp = thpool_init(2);
#endif

    char *alexName = "Alex";
    char *vggName = "VGG";
    char *denseName = "Dense";
    char *resName = "Res";

    int n_des = atoi(argv[3]);
    int n_res = atoi(argv[4]);
    int n_vgg = atoi(argv[5]);
    int n_alex = atoi(argv[6]);
    
    network *denseNetwork[n_des];
    network *resNetwork[n_res];
    network *vggNetwork[n_vgg];
    network *alexNetwork[n_alex];

    int i = 0;
    n_total = n_des+n_res+n_vgg+n_alex;
    cudnn_handle_set_stream(n_total);
#ifdef THREAD
    //변수 동적할당
    cond_t = (pthread_cond_t *)malloc(sizeof(pthread_cond_t) * n_total);
    mutex_t = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t) * n_total);
    cond_i = (int *)malloc(sizeof(int) * n_total);
    start_flag = 0;
    for (i = 0; i < n_total; i++)
    {
        pthread_cond_init(&cond_t[i], NULL);
        pthread_mutex_init(&mutex_t[i], NULL);
        cond_i[i] = 0;
        sync_time_list[i] = 0;
    }
#endif
    int k;
     for (k = 0; k < n_des; k++)
    {
        denseNetwork[k] = (network *)load_network("cfg/densenet201.cfg", "weights/densenet201.weights", 0);
        denseNetwork[k]->index_n = k;
    }

    for (k = 0; k < n_res; k++)
    {
        resNetwork[k] = (network *)load_network("cfg/resnet152.cfg", "weights/resnet152.weights", 0);
        resNetwork[k]->index_n = k + n_des;
    }
    for (k = 0; k < n_vgg; k++)
    {
        vggNetwork[k] = (network *)load_network("cfg/vgg-16.cfg", "weights/vgg-16.weights", 0);
        vggNetwork[k]->index_n = k + n_des + n_res;
    }
    for (k = 0; k < n_alex; k++)
    {
        alexNetwork[k] = (network *)load_network("cfg/alexnet.cfg", "weights/alexnet.weights", 0);
        alexNetwork[k]->index_n = k + n_des + n_res + n_vgg;
    }

    list *options = read_data_cfg("cfg/imagenet1k.data");
    char *name_list = option_find_str(options, "names", 0);
    if (!name_list)
        name_list = option_find_str(options, "labels", "data/labels.list");
    int top = option_find_int(options, "top", 1);

    char **names = get_labels(name_list);

    char *buff = "data/eagle.jpg";
    //char *input = buff;
    test *net_input_des[n_des];
    test *net_input_res[n_res];
    test *net_input_vgg[n_vgg];
    test *net_input_alex[n_alex];

    image im = load_image_color(buff, 0, 0);

    start_time_a = what_time_is_it_now();
    start_time = what_time_is_it_now();


    pthread_t networkArray_des[n_des];
    pthread_t networkArray_res[n_res];
    pthread_t networkArray_vgg[n_vgg];
    pthread_t networkArray_alex[n_alex];

    for (i = 0; i < n_des; i++)
    {
        net_input_des[i] = (test *)malloc(sizeof(test));
        net_input_des[i]->net = denseNetwork[i];
        net_input_des[i]->input_path = buff;
        net_input_des[i]->names = names;
        net_input_des[i]->netName = denseName;

        fprintf(stderr, " It's turn for des i = %d\n", i);
        if (pthread_create(&networkArray_des[i], NULL, (void *)predict_classifier2, net_input_des[i]) < 0)
        {
            perror("thread error");
            exit(0);
        }
    }

    for (i = 0; i < n_res; i++)
    {
        net_input_res[i] = (test *)malloc(sizeof(test));
        net_input_res[i]->net = resNetwork[i];
        net_input_res[i]->input_path = buff;
        net_input_res[i]->names = names;
        net_input_res[i]->netName = resName;

        fprintf(stderr," It's turn for res i = %d\n", i);
        if (pthread_create(&networkArray_res[i], NULL, (void *)predict_classifier2, net_input_res[i]) < 0)
        {
            perror("thread error");
            exit(0);
        }
    }

    for (i = 0; i < n_vgg; i++)
    {
            net_input_vgg[i] = (test *)malloc(sizeof(test));
            net_input_vgg[i]->net = vggNetwork[i];
            net_input_vgg[i]->input_path = buff;
            net_input_vgg[i]->names = names;
            net_input_vgg[i]->netName = vggName;

        fprintf(stderr, " It's turn for vgg i = %d\n", i);
        if (pthread_create(&networkArray_vgg[i], NULL, (void *)predict_classifier2, net_input_vgg[i]) < 0)
        {
            perror("thread error");
            exit(0);
        }
    }

    for (i = 0; i < n_alex; i++)
    {
            net_input_alex[i] = (test *)malloc(sizeof(test));
            net_input_alex[i]->net = alexNetwork[i];
            net_input_alex[i]->input_path = buff;
            net_input_alex[i]->names = names;
            net_input_alex[i]->netName = alexName;

        fprintf(stderr, " It's turn for alex i = %d\n", i);
        if (pthread_create(&networkArray_alex[i], NULL, (void *)predict_classifier2, net_input_alex[i]) < 0)
        {
            perror("thread error");
            exit(0);
        }
    }
    
    //fprintf(stderr, "\n execution Time : %lf\n", what_time_is_it_now() - time);

    start_flag = 1;
    for (i = 0; i < n_des; i++)
    {
        pthread_join(networkArray_des[i], NULL);
    }
    for (i = 0; i < n_res; i++)
    {
        pthread_join(networkArray_res[i], NULL);
    }
    for (i = 0; i < n_vgg; i++)
    {
        pthread_join(networkArray_vgg[i], NULL);
    }
    for (i = 0; i < n_alex; i++)
    {
        pthread_join(networkArray_alex[i], NULL);
    }
    double end = what_time_is_it_now();
    fprintf(stderr,"\n execution Time : %lf\n", end - start_time_a);

}
